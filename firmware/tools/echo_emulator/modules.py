from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_R0

import make_image

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
        self.machine.hook_symbol(LOADER, self.entered)
        self.machine.uc.hook_add(UC_HOOK_MEM_WRITE, self.slot_written, None, SLOT_FIRST, SLOT_LAST)

    def count_slot_code(self):
        def hook(uc, where, size, user):
            self.executed += 1

        self.machine.uc.hook_add(UC_HOOK_CODE, hook, None, SLOT_FIRST, SLOT_LAST)
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
