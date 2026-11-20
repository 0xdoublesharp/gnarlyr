from pathlib import Path

from unicorn import UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_R0

import make_image

from . import direct

SLOT_FIRST = make_image.MODULE_BASE
SLOT_LAST = make_image.MODULE_BASE + make_image.MODULE_CAPACITY - 1
LOADER = "hal_system_load_module"
SECTOR_BYTES = 512


class ModuleWatch:
    """Follows the module loader and the slot: every call of the loader with what it read and
    how long it took, how many instructions run in the slot, and what writes into it.

    Outside a load nothing may write into the loaded module's code and constants
    (`code_writes`). Its zeroed memory behind them is written by the module and by what it
    calls, so those writes are only counted (`other_writes`): with no module screen open the
    count must stand still.
    """

    def __init__(self, session):
        self.session = session
        self.machine = session.machine
        self.calls = []
        self.pending = None
        self.returns = set()
        self.code_end = SLOT_FIRST
        self.code_writes = []
        self.other_writes = 0
        self.executed = 0
        self.count_after = None
        self.machine.hook_symbol(LOADER, self.entered)
        direct.hook_add(self.machine.uc, UC_HOOK_MEM_WRITE, self.slot_written, None, SLOT_FIRST, SLOT_LAST)

    def count_slot_code(self, after_module=None):
        if after_module is not None:
            self.count_after = after_module
            return
        def hook(uc, where, size, user):
            self.executed += 1

        direct.hook_add(self.machine.uc, UC_HOOK_CODE, hook, None, SLOT_FIRST, SLOT_LAST)
        self.machine.uc.ctl_remove_cache(SLOT_FIRST, SLOT_LAST + 1)

    def entered(self):
        machine = self.machine
        back = machine.reg(UC_ARM_REG_LR) & ~1
        self.pending = {"module": machine.reg(UC_ARM_REG_R0), "start_us": machine.microseconds(),
                        "reads": len(self.session.card.reads)}
        if back not in self.returns:
            self.returns.add(back)
            machine.add_address_hook(back, self.returned)

    def returned(self):
        call, self.pending = self.pending, None
        if call is None:
            return
        reads = self.session.card.reads[call.pop("reads"):]
        call["sectors"] = sum(count for _, _, count in reads)
        call["commands"] = len(reads)
        call["us"] = self.machine.microseconds() - call.pop("start_us")
        call["loaded"] = self.machine.reg(UC_ARM_REG_R0) != 0
        if call["sectors"]:
            size = int.from_bytes(self.machine.read_memory(SLOT_FIRST + 4, 4), "little")
            self.code_end = SLOT_FIRST + size if call["loaded"] else SLOT_FIRST
        self.calls.append(call)
        if self.count_after == call["module"]:
            self.count_after = None
            self.count_slot_code()

    def slot_written(self, uc, access, address, size, value, user):
        if self.pending is not None:
            return
        if address < self.code_end:
            self.code_writes.append((address, self.machine.symbol_at(self.machine.current_pc())))
        else:
            self.other_writes += 1

    def loads(self):
        """The calls that read storage: the ones that put a module into the slot or tried to."""
        return [call for call in self.calls if call["sectors"]]

    def describe(self, call):
        return "module %d: %d bytes in %d commands, %.1f ms, %s" % (
            call["module"], call["sectors"] * SECTOR_BYTES, call["commands"], call["us"] / 1000,
            "loaded" if call["loaded"] else "refused")


SLOT_MODULE_NAMES = {1: "views", 2: "settings", 3: "library", 4: "listening", 9: "equalizer",
                     11: "playing", 16: "stage"}
ELF_FUNCTION = 2
LOADER_FRAMES = ("board_overlay", "hal_system_load_module", "load_module", "read_rest",
                 "body_intact", "already_loaded", "header_size", "image_crc_update", "clear")


def module_layout(elf_path, first, last):
    """The function entries and the end of the memory a module ELF uses inside its window."""
    elf = Path(elf_path).read_bytes()
    header = make_image.ELF_HEADER.unpack_from(elf)
    table, count = header[6], header[12]
    sections = [make_image.ELF_SECTION.unpack_from(elf, table + index * make_image.ELF_SECTION.size)
                for index in range(count)]
    end = first
    for section in sections:
        if section[2] & make_image.SHF_ALLOC and first <= section[3] <= last:
            end = max(end, section[3] + section[5])
    functions = {}
    for section in sections:
        if section[1] != 2:
            continue
        names = sections[section[6]]
        for position in range(section[5] // 16):
            name, value, _, info, _, _ = make_image.ELF_SYMBOL.unpack_from(elf, section[4] + position * 16)
            start = names[4] + name
            label = elf[start:elf.index(b"\x00", start)].decode("ascii")
            if label and info & 0xF == ELF_FUNCTION and first <= value & ~1 <= last:
                functions.setdefault(value & ~1, label)
    return functions, end


def address_taken(content, base, functions):
    """Function entries whose Thumb address appears as a word in the module: the ones a pointer
    can hold, so the only ones a stale callback can reach."""
    taken = set()
    for offset in range(0, len(content) - 3, 2):
        word = int.from_bytes(content[offset:offset + 4], "little")
        if word & 1 and word & ~1 in functions:
            taken.add(word & ~1)
    return taken


class SlotOwnership:
    """Follows which module owns the shared UI slot and faults on use of a module that no longer
    owns it.

    All UI modules link at the same address, so a stale pointer cannot be told apart by its
    address alone. What is detectable: a call from resident code to an address that some module
    stores as a function pointer but that is not a function entry of the module now in the slot;
    any execution or read in the slot while it holds no module (after a refused load); and a
    read past the end of the owning module's code, data and BSS.
    """

    def __init__(self, machine, stop=True):
        self.machine = machine
        self.stop = stop
        self.owner = None
        self.state = "empty"
        self.pending = None
        self.returns = set()
        self.history = []
        self.faults = []
        self.empty_hooks = []
        self.extent_hook = None
        self.layouts = {}
        taken = set()
        for overlay in machine.modules:
            if overlay.base != SLOT_FIRST or overlay.number not in SLOT_MODULE_NAMES:
                continue
            functions, end = module_layout(overlay_path(overlay), SLOT_FIRST, SLOT_LAST)
            self.layouts[overlay.number] = (functions, end)
            taken |= address_taken(overlay.stored, overlay.base, functions)
        self.taken = taken
        machine.hook_symbol(LOADER, self.load_entered)
        direct.hook_add(machine.uc, UC_HOOK_MEM_WRITE, self.slot_written, None, SLOT_FIRST,
                        SLOT_FIRST + 3)
        for address in sorted(taken):
            machine.add_address_hook(address, lambda address=address: self.entered(address))
        machine.hook_symbol("app_resume_after_usb", self.usb_left, optional=True)
        self.arm()

    def name(self, number):
        return SLOT_MODULE_NAMES.get(number, "module %s" % number) if number else "no module"

    def describe(self, address):
        if SLOT_FIRST <= address <= SLOT_LAST:
            owners = ["%s:%s" % (self.name(number), functions[address])
                      for number, (functions, _) in sorted(self.layouts.items())
                      if address in functions]
            return "%#x (%s)" % (address, ", ".join(owners) or "inside a module function")
        return "%#x (%s)" % (address, self.machine.symbol_at(address))

    def fault(self, kind, address):
        machine = self.machine
        pc = machine.current_pc()
        lr = machine.reg(UC_ARM_REG_LR) & ~1
        recent = ", ".join("%d ms %s" % item for item in self.history[-4:])
        report = ("%s at %s; the slot holds %s (%s)\n  pc %s\n  lr %s\n  recent: %s" % (
            kind, self.describe(address), self.name(self.owner), self.state, self.describe(pc),
            self.describe(lr), recent))
        self.faults.append(report)
        if self.stop:
            machine.stop("slot ownership: " + report)

    def record(self, event):
        self.history.append((self.machine.milliseconds(), event))
        del self.history[:-16]

    def load_entered(self):
        machine = self.machine
        back = machine.reg(UC_ARM_REG_LR) & ~1
        self.pending = machine.reg(UC_ARM_REG_R0)
        if back not in self.returns:
            self.returns.add(back)
            machine.add_address_hook(back, self.load_returned)

    def load_returned(self):
        if self.pending is None:
            return
        module, self.pending = self.pending, None
        loaded = self.machine.reg(UC_ARM_REG_R0) != 0
        if loaded and module in self.layouts:
            if module != self.owner:
                self.record("loaded " + self.name(module))
            self.owner, self.state = module, "loaded"
        elif loaded:
            self.owner, self.state = None, "foreign module %d" % module
            self.record("loaded module %d" % module)
        else:
            self.owner, self.state = None, "empty"
            self.record("refused " + self.name(module))
        self.arm()

    def slot_written(self, uc, access, address, size, value, user):
        if self.pending is not None or self.state == "overlay":
            return
        self.owner, self.state = None, "overlay"
        self.record("overwritten by " + self.machine.symbol_at(self.machine.current_pc()))
        self.arm()

    def usb_left(self):
        if self.state == "overlay":
            self.state = "empty"
            self.record("USB session over")
            self.arm()

    def entered(self, address):
        lr = self.machine.reg(UC_ARM_REG_LR) & ~1
        if self.state == "overlay":
            return
        if self.owner is None:
            self.fault("call into the UI slot with no module loaded", address)
        elif address not in self.layouts[self.owner][0] and not SLOT_FIRST <= lr <= SLOT_LAST:
            self.fault("call from resident code to a function of a module that was evicted", address)

    def in_loader(self):
        if self.pending is not None:
            return True
        name = self.machine.symbol_at(self.machine.current_pc())
        return name.startswith(LOADER_FRAMES)

    def empty_executed(self, uc, address, size, user):
        self.fault("execution in the UI slot with no module loaded", address)

    def empty_read(self, uc, access, address, size, value, user):
        if not self.in_loader():
            self.fault("read of the UI slot with no module loaded", address)

    def stale_read(self, uc, access, address, size, value, user):
        if not self.in_loader():
            self.fault("read past the end of the %s module" % self.name(self.owner), address)

    def arm(self):
        uc = self.machine.uc
        for handle in self.empty_hooks:
            uc.hook_del(handle)
        self.empty_hooks = []
        if self.extent_hook is not None:
            uc.hook_del(self.extent_hook)
            self.extent_hook = None
        if self.state == "empty":
            self.empty_hooks = [
                direct.hook_add(uc, UC_HOOK_BLOCK, self.empty_executed, None, SLOT_FIRST, SLOT_LAST),
                direct.hook_add(uc, UC_HOOK_MEM_READ, self.empty_read, None, SLOT_FIRST, SLOT_LAST)]
            uc.ctl_remove_cache(SLOT_FIRST, SLOT_LAST + 1)
        elif self.owner is not None:
            end = self.layouts[self.owner][1]
            if end <= SLOT_LAST:
                self.extent_hook = direct.hook_add(uc, UC_HOOK_MEM_READ, self.stale_read, None,
                                                   end, SLOT_LAST)


def overlay_path(overlay):
    return overlay.elf_path
