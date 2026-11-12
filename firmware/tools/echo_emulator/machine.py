import struct
import sys
from pathlib import Path

from unicorn import (
    UC_ARCH_ARM, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_FETCH_UNMAPPED,
    UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED, UC_MODE_MCLASS, UC_MODE_THUMB, Uc,
    UcError)
from unicorn.arm_const import (
    UC_ARM_REG_LR, UC_ARM_REG_MSP, UC_ARM_REG_PC, UC_ARM_REG_PRIMASK, UC_ARM_REG_R0,
    UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_XPSR,
    UC_CPU_ARM_CORTEX_M3)

from . import devices
from .panel import Panel

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import make_image

RAM_BASE = 0x03000000
RAM_SIZE = 0x100000
TRAMPOLINE = 0x03FF0000
STUB_RETURN = TRAMPOLINE + 0x800
RETURN_INSTRUCTION = b"\x70\x47"
CORE1_RAM_BASE = 0x01000000
CORE1_RAM_SIZE = 0x60000
PAGE = 0x1000
SLICE_INSTRUCTIONS = 4000
MASKED_SLICE_INSTRUCTIONS = 5
CRYSTAL_HZ = 24000000
CORE_REGISTERS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R12,
                  UC_ARM_REG_LR)
SYSTICK_VECTOR_INDEX = 15
SYSTICK_HANDLER_NAME = "device_systick_entry"
PERIPHERAL_FIRST = 0x40000000
PERIPHERAL_LAST = 0x80000000
POWER_KEY_PORT = 2
POWER_KEY_PIN = 5
CHARGE_PORT = 2
CHARGE_PIN = 3
CARD_DETECT_PORT = 0
CARD_DETECT_PIN = 11
BALANCED_PORT = 2
BALANCED_PIN = 8
ADC_NO_KEY = 1023
PLAY_KEY_ADC = 250
VOLUME_UP_KEY_ADC = 120
BACK_KEY_ADC = 500
USB_ANSWER_FILE_TRANSFER = 1
PROMPT_KEY_GAP_MS = 300
PROMPT_KEY_HOLD_MS = 120
FAILURE_SYMBOLS = ("board_panic_fault", "board_panic_stall", "device_default_handler",
                   "device_fault_entry", "hal_power_off", "board_power_cut")
IDLE_POLL_SYMBOLS = ("board_input_take_key_press",)
ADC_BATTERY_4000_MV = 819
HOST_GONE_MS = 40


def load_symbols(elf_path, first=0, last=0xFFFFFFFF):
    elf = Path(elf_path).read_bytes()
    header = make_image.ELF_HEADER.unpack_from(elf)
    table, count = header[6], header[12]
    sections = [make_image.ELF_SECTION.unpack_from(elf, table + index * make_image.ELF_SECTION.size)
                for index in range(count)]
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        names = sections[section[6]]
        for position in range(section[5] // 16):
            name, value, size, info, other, shndx = struct.unpack_from(
                "<IIIBBH", elf, section[4] + position * 16)
            start = names[4] + name
            label = elf[start:elf.index(b"\x00", start)].decode("ascii")
            kind = info & 0xF
            if label and kind in (0, 1, 2) and first <= value < last:
                symbols[label] = value if kind == 1 else value & ~1
    return symbols


class Overlay:
    """A separately linked program the firmware loads into high RAM on demand.

    Its addresses are shared with the other overlays and modules and with decoder data, so a
    name in it only counts while the memory holds exactly the copy stored in the image.
    """

    def __init__(self, image, elf_path, image_offset, base=make_image.OVERLAY_BASE,
                 capacity=make_image.OVERLAY_CAPACITY):
        content = make_image.elf_sections(Path(elf_path).read_bytes())[".overlay"].content
        header = make_image.OVERLAY_HEADER_SIZE
        self.base = base
        self.stored = image[image_offset:image_offset + len(content)]
        if len(content) <= header or self.stored[header:] != content[header:]:
            raise ValueError("the overlay ELF does not belong to the image")
        self.image_offset = image_offset
        self.name = Path(elf_path).stem
        self.symbols = load_symbols(elf_path, base, base + capacity)
        self.names = {}
        for name, address in self.symbols.items():
            self.names.setdefault(address, name)
        self.header = self.stored[:header]

    def loaded(self, machine):
        return (machine.read_memory(self.base, len(self.header)) == self.header
                and machine.read_memory(self.base, len(self.stored)) == self.stored)


def module_overlay(image, offset, module_elfs):
    number = image[offset + 3]
    base, capacity = make_image.module_window(number)
    for elf_path in module_elfs:
        try:
            overlay = Overlay(image, elf_path, offset, base, capacity)
            overlay.number = number
            return overlay
        except ValueError:
            continue
    raise ValueError("none of the module ELFs belongs to the module at %#x of the image" % offset)


def module_overlays(image, module_elfs):
    """The image's modules in the order of their numbers, each with the ELF it was built from."""
    return [module_overlay(image, offset, module_elfs)
            for offset, size in make_image.modules_in(image) if size]


class Machine:
    def __init__(self, image, elf_path, card=None, usb_overlay_elf=None, rest_in_waits=True,
                 module_elfs=(), optional_hooks=()):
        self.optional_hooks = optional_hooks
        self.image = image
        self.elf_path = elf_path
        self.symbols = load_symbols(elf_path)
        self.overlays = []
        if usb_overlay_elf is not None:
            self.overlays.append(Overlay(image, usb_overlay_elf, make_image.USB_OVERLAY_OFFSET))
        self.modules = module_overlays(image, module_elfs)
        self.overlays += self.modules
        self.names = {}
        for name, address in self.symbols.items():
            self.names.setdefault(address, name)
        self.card = card
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
        self.base_cycles = 0
        self.jump_cycles = 0
        self.core_hz = CRYSTAL_HZ
        self.rate_mark_cycles = 0
        self.rate_mark_us = 0
        self.clock_log = []
        self.vbus = False
        self.adc_key_value = ADC_NO_KEY
        self.adc_battery_value = ADC_BATTERY_4000_MV
        self.power_key_released_at_ms = 300
        self.card_present = False
        self.balanced_plugged = False
        self.gpio_log = []
        self.grf_log = []
        self.pwm_log = []
        self.pmu_flag_writes = []
        self.unknown_mmio = set()
        self.faults = []
        self.watchdog_kicks = 0
        self.watchdog_running = False
        self.watchdog_resets = 0
        self.traces = {}
        self.reset_requested = None
        self.stop_reason = None
        self.exception_depth = 0
        self.pending_return = False
        self.panel = Panel(self)
        self.generic_pages = {}
        self.usb_reset_ack = False
        self.scheduled = []
        self.stamps = 0
        self.panics = []
        self.reached = {}
        self.usb_connect_count = 0
        self.usb_log = []
        self.register_log = []
        self.coast_log = []
        self.host_stays = False
        self.host_absent = False
        self.usb_pulled_up = False
        self.host_ejected = False
        self.cable_bit_dies_with_usb = False
        self.cable_bit_dead = False
        self.usb_answer = USB_ANSWER_FILE_TRANSFER
        self.entries = {}
        self.entered = None
        self.parked = None
        self.interrupted_parked = None
        self.resumed = None
        self.halting = False
        self.build_memory()
        self.load_firmware()
        self.install_hooks()
        self.watch_failures()
        if rest_in_waits:
            self.rest_at_idle_polls()

    def cycles(self):
        return self.base_cycles + self.jump_cycles

    def stamp(self):
        self.stamps += 1
        return self.stamps

    def poll_jump(self, cycles):
        self.jump_cycles += cycles

    def microseconds(self):
        since = self.cycles() - self.rate_mark_cycles
        return self.rate_mark_us + since * 1000000 // self.core_hz

    def milliseconds(self):
        return self.microseconds() // 1000

    def set_core_hz(self, hz):
        self.rate_mark_us = self.microseconds()
        self.rate_mark_cycles = self.cycles()
        self.core_hz = hz
        self.clock_log.append((self.rate_mark_us // 1000, hz))

    def external_pin_levels(self, port):
        levels = 0
        if port == CHARGE_PORT:
            levels |= 1 << CHARGE_PIN
            if self.milliseconds() < self.power_key_released_at_ms:
                levels |= 1 << POWER_KEY_PIN
        if port == CARD_DETECT_PORT and not self.card_present:
            levels |= 1 << CARD_DETECT_PIN
        if port == BALANCED_PORT and self.balanced_plugged:
            levels |= 1 << BALANCED_PIN
        return levels

    def clock_changed(self):
        cru = self.cru.regs
        mode = cru.get(devices.CRU_MODE, 0)
        if not mode & 1:
            hz = CRYSTAL_HZ
        else:
            con0, con1 = cru.get(0x00, 0), cru.get(0x04, 0)
            fbdiv, postdiv1 = con0 & 0xFFF, (con0 >> 12) & 7
            refdiv, postdiv2 = con1 & 0x3F, (con1 >> 6) & 7
            if 0 in (fbdiv, postdiv1, refdiv, postdiv2):
                return
            hz = CRYSTAL_HZ // refdiv * fbdiv // postdiv1 // postdiv2
            hz //= (cru.get(devices.CRU_CLOCK_SELECT, 0) & devices.CORE_DIVIDER_MASK) + 1
        self.set_core_hz(hz)
        self.systick.restart()

    def pclk_hz(self):
        shift = (self.cru.regs.get(devices.CRU_PCLK_SELECT, 0) >> 8) & 3
        return self.core_hz >> shift if self.cru.regs.get(devices.CRU_MODE, 0) & 1 else CRYSTAL_HZ

    def map_device(self, device, base, size):
        def read(uc, offset, width, user):
            return device.read(offset, width)

        def write(uc, offset, width, value, user):
            device.write(offset, width, value)

        self.uc.mmio_map(base, size, read, None, write, None)

    def build_memory(self):
        self.uc.mem_map(RAM_BASE, RAM_SIZE)
        self.uc.mem_map(TRAMPOLINE, PAGE)
        self.uc.mem_map(CORE1_RAM_BASE, CORE1_RAM_SIZE)
        self.uc.mem_write(TRAMPOLINE, b"\x00\xbf" * (PAGE // 2))
        self.uc.mem_write(STUB_RETURN, RETURN_INSTRUCTION)
        self.gpio = [devices.Gpio(self, port) for port in range(3)]
        self.cru = devices.Cru(self)
        self.systick = devices.Systick(self)
        self.usb = devices.UsbCore(self)
        self.pmu = devices.Pmu(self)
        self.vop = devices.Vop(self)
        from .emmc import DwMmc
        self.watchdog = devices.Watchdog(self)
        self.emmc_controller = DwMmc(self, self.card)
        self.sd_controller = DwMmc(self, None)
        self.grf = devices.Grf(self)
        self.rtc_bus = devices.RtcBus(self)
        mapping = [
            (self.gpio[0], 0x40160000, PAGE), (self.gpio[1], 0x40170000, PAGE),
            (self.gpio[2], 0x50030000, PAGE), (self.grf, 0x50010000, PAGE),
            (self.pmu, 0x50020000, PAGE), (self.cru, 0x40180000, PAGE),
            (devices.Pwm(self), 0x400F0000, PAGE), (devices.SarAdc(self), 0x400D0000, PAGE),
            (self.watchdog, 0x40120000, PAGE), (self.systick, 0xE000E000, PAGE),
            (self.vop, 0x60070000, PAGE), (devices.Dmac(), 0x60000000, PAGE),
            (self.usb, 0x60080000, 0x10000), (self.emmc_controller, 0x60030000, PAGE),
            (self.sd_controller, 0x60020000, PAGE), (self.rtc_bus, 0x40050000, PAGE),
        ]
        from .bluetooth import UART_BASE, BluetoothModule
        self.bluetooth = BluetoothModule(self)
        mapping.append((self.bluetooth, UART_BASE, PAGE))
        for device, base, size in mapping:
            self.map_device(device, base, size)

    def load_firmware(self):
        image = self.image
        rows = make_image.split_image(image).rows
        base, offset = make_image.CODE_BASE, 0x200
        for first in range(0, len(rows) - 1, 2):
            code_load, code_exec, code_len, data_load = rows[first]
            data_exec, data_len, bss_exec, bss_len = rows[first + 1]
            if not any(rows[first]) and not any(rows[first + 1]):
                break
            if code_len:
                start = code_load - base + offset
                self.uc.mem_write(code_exec, image[start:start + code_len])
            if bss_len:
                self.uc.mem_write(bss_exec, bytes(bss_len))
            if data_len:
                start = data_load - base + offset
                self.uc.mem_write(data_exec, image[start:start + data_len])
        self.check_elf_matches()
        stack, reset = struct.unpack("<II", self.uc.mem_read(make_image.DATA_BASE, 8))
        self.uc.reg_write(UC_ARM_REG_SP, stack)
        self.entry = reset & ~1

    def check_elf_matches(self):
        code = make_image.elf_sections(Path(self.elf_path).read_bytes())[".code"].content
        loaded = bytes(self.uc.mem_read(make_image.CODE_BASE, len(code)))
        if loaded != code:
            raise ValueError("the ELF does not belong to the image")

    def unmapped(self, uc, access, address, size, value, user):
        page = address & ~(PAGE - 1)
        if PERIPHERAL_FIRST <= address < PERIPHERAL_LAST and page not in self.generic_pages:
            self.generic_pages[page] = devices.GenericPage(self, page)
            self.map_device(self.generic_pages[page], page, PAGE)
            return True
        self.faults.append((self.cycles(), "unmapped access", address, self.current_pc()))
        return False

    def install_hooks(self):
        self.uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED
                         | UC_HOOK_MEM_FETCH_UNMAPPED, self.unmapped)
        self.add_address_hook(TRAMPOLINE, self.exception_return)
        self.hook_symbol("hal_system_set_usb_answer", self.override_usb_answer,
                         optional="hal_system_set_usb_answer" in self.optional_hooks)

    def override_usb_answer(self):
        """Stand in for the owner's When plugged in setting; None keeps the firmware's own."""
        if self.usb_answer is not None:
            self.uc.reg_write(UC_ARM_REG_R0, self.usb_answer)

    def watch_failures(self):
        for name in FAILURE_SYMBOLS:
            self.hook_symbol(name, self.failure_hook(name))

    def rest_at_idle_polls(self):
        for name in IDLE_POLL_SYMBOLS:
            self.hook_symbol(name, self.uc.emu_stop)

    def failure_hook(self, name):
        def failed():
            self.panics.append(name)
            self.stop("firmware reached " + name)

        return failed

    def add_address_hook(self, address, callback):
        callbacks = self.entries.get(address)
        if callbacks is not None:
            callbacks.append(callback)
            return
        callbacks = self.entries[address] = [callback]

        def hook(uc, where, size, user):
            if self.resumed == where:
                self.resumed = None
                return
            self.entered = where
            for entry in callbacks:
                entry()
                if self.halting:
                    break

        self.uc.hook_add(UC_HOOK_BLOCK, hook, None, address, address)
        self.uc.ctl_remove_cache(address, address + 2)

    def hook_symbol(self, name, callback, optional=False):
        if name in self.symbols:
            self.add_address_hook(self.symbols[name], callback)
            return
        overlay = next((item for item in self.overlays if name in item.symbols), None)
        if overlay is None:
            if optional:
                return
            raise KeyError(f"{name} is not in the image: mark it CORE_SEAM so the emulator can reach it")
        self.add_address_hook(overlay.symbols[name],
                              lambda: callback() if overlay.loaded(self) else None)

    def stub_symbol(self, name, handler):
        def replaced():
            result = handler(self)
            if result is not None:
                self.uc.reg_write(UC_ARM_REG_R0, result & 0xFFFFFFFF)
            self.uc.reg_write(UC_ARM_REG_PC, STUB_RETURN | 1)

        self.hook_symbol(name, replaced)

    def current_pc(self):
        return self.uc.reg_read(UC_ARM_REG_PC)

    def reg(self, register):
        return self.uc.reg_read(register)

    def read_memory(self, address, length):
        return bytes(self.uc.mem_read(address, length))

    def read_cstring(self, address):
        data = b""
        while True:
            byte = self.read_memory(address + len(data), 1)
            if byte == b"\x00" or len(data) > 256:
                return data
            data += byte

    def stop(self, reason):
        self.stop_reason = reason
        self.halting = True
        self.uc.emu_stop()

    def continue_past_stop(self):
        """Let the next run go on from a function whose hook stopped the last one on entry."""
        self.parked = self.uc.reg_read(UC_ARM_REG_PC)

    def system_reset_requested(self):
        self.reset_requested = self.cycles()
        self.stop("system reset requested")

    def host_ejects(self):
        """The host stays plugged in and ejects every drive, where it otherwise unplugs.

        No SCSI traffic is emulated, so the eject is given as the answer of `msc_finished`,
        the question the session asks on every pass.
        """
        self.host_stays = True
        self.stub_symbol("msc_finished", lambda machine: int(machine.host_ejected))

    def cable_bit(self):
        """What the firmware reads as the cable: on the player the bit stops reporting once
        the firmware has powered USB down, with the cable still in."""
        return self.vbus and not self.cable_bit_dead

    def usb_powered_down(self):
        if self.cable_bit_dies_with_usb:
            self.cable_bit_dead = True

    def host_leaves(self):
        if self.host_stays:
            self.host_ejected = True
            self.usb_log.append((self.milliseconds(), "host ejects"))
        else:
            self.vbus = False
            self.usb_log.append((self.milliseconds(), "cable pulled"))

    def usb_reset_acknowledged(self):
        self.usb_reset_ack = True
        self.usb_log.append((self.milliseconds(), "bus reset acknowledged"))
        self.at_ms(self.milliseconds() + HOST_GONE_MS, self.host_leaves)

    def host_answers(self):
        return self.vbus and not self.host_absent

    def usb_connected(self):
        self.usb_connect_count += 1
        self.usb_pulled_up = True
        self.host_ejected = False
        self.usb_log.append((self.milliseconds(), "pull-up on" + (", host resets the bus"
                                                                  if self.host_answers() else "")))
        if self.host_answers():
            self.usb.raise_reset()

    def host_arrives(self):
        """A host takes the place of a charger on a cable that is already in."""
        self.host_absent = False
        if self.usb_pulled_up and self.vbus:
            self.usb_log.append((self.milliseconds(), "host arrives and resets the bus"))
            self.usb.raise_reset()

    def usb_disconnected(self):
        self.usb_pulled_up = False
        self.usb_log.append((self.milliseconds(), "soft disconnect"))
        self.register_log.append(("usb", "soft disconnect", 0))

    def at_ms(self, milliseconds, action):
        self.scheduled.append((milliseconds, action))

    def run_scheduled(self):
        now = self.milliseconds()
        due = [item for item in self.scheduled if item[0] <= now]
        for item in due:
            self.scheduled.remove(item)
            item[1]()

    def stop_at(self, name):
        def reached():
            self.reached[name] = self.reached.get(name, 0) + 1
            self.stop("reached " + name)

        self.hook_symbol(name, reached)

    def press_key(self, adc_value, delay_ms=20, hold_ms=300):
        start = self.milliseconds() + delay_ms
        self.at_ms(start, lambda: setattr(self, "adc_key_value", adc_value))
        self.at_ms(start + hold_ms, lambda: setattr(self, "adc_key_value", ADC_NO_KEY))

    def answer_waits_for_key(self, presses):
        def waiting():
            self.reached["board_wait_for_key"] = self.reached.get("board_wait_for_key", 0) + 1
            if self.reached["board_wait_for_key"] > presses:
                self.stop("reached board_wait_for_key")
            else:
                self.press_key(PLAY_KEY_ADC)

        self.hook_symbol("board_wait_for_key", waiting)

    def answer_usb_prompt(self, keys, times=1):
        """Press `keys` (ADC values) each time the plug-in prompt opens, up to `times` times."""
        def asked():
            self.reached["app_ask_usb_mode"] = self.reached.get("app_ask_usb_mode", 0) + 1
            if self.reached["app_ask_usb_mode"] > times:
                return
            for index, value in enumerate(keys):
                self.press_key(value, PROMPT_KEY_GAP_MS * (index + 1), PROMPT_KEY_HOLD_MS)

        self.hook_symbol("app_ask_usb_mode", asked)

    def trace(self, name):
        stamps = self.traces.setdefault(name, [])
        self.hook_symbol(name, lambda: stamps.append(self.milliseconds()))
        return stamps

    def note(self, name):
        def reached():
            self.reached[name] = self.reached.get(name, 0) + 1

        self.hook_symbol(name, reached)

    def names_at(self, address):
        for overlay in self.overlays:
            if overlay.base <= address < overlay.base + len(overlay.stored) and overlay.loaded(self):
                return overlay.names
        return self.names

    def count_window_code(self, first=make_image.OVERLAY_BASE,
                          capacity=make_image.OVERLAY_CAPACITY):
        counter = [0]

        def hook(uc, where, size, user):
            counter[0] += 1

        self.uc.hook_add(UC_HOOK_CODE, hook, None, first, first + capacity - 1)
        self.uc.ctl_remove_cache(first, first + capacity)
        return counter

    def collect_notices(self):
        notices = []

        def shown():
            title = self.read_cstring(self.reg(UC_ARM_REG_R0)).decode("utf-8", "replace")
            message = self.read_cstring(self.reg(UC_ARM_REG_R1)).decode("utf-8", "replace")
            notices.append((title, message))

        self.hook_symbol("app_show_notice", shown)
        return notices

    def symbol_at(self, address):
        best = None
        for candidate, name in self.names_at(address).items():
            if candidate <= address and (best is None or candidate > best[0]):
                best = (candidate, name)
        return "%s+%#x" % (best[1], address - best[0]) if best else "?"

    def interrupt_due(self):
        systick = self.systick
        return (systick.tick_interrupt and systick.enabled and systick.pending()
                and not self.uc.reg_read(UC_ARM_REG_PRIMASK) and self.exception_depth == 0)

    def enter_systick(self):
        vtor = self.systick.vtor
        handler = struct.unpack("<I", self.read_memory(vtor + 4 * SYSTICK_VECTOR_INDEX, 4))[0]
        sp = self.uc.reg_read(UC_ARM_REG_MSP) - 32
        pc = self.uc.reg_read(UC_ARM_REG_PC)
        frame = [self.uc.reg_read(register) for register in CORE_REGISTERS]
        frame += [pc | 1, self.uc.reg_read(UC_ARM_REG_XPSR)]
        self.uc.mem_write(sp, struct.pack("<8I", *frame))
        self.uc.reg_write(UC_ARM_REG_MSP, sp)
        self.uc.reg_write(UC_ARM_REG_LR, TRAMPOLINE | 1)
        self.uc.reg_write(UC_ARM_REG_PC, handler & ~1)
        self.systick.acknowledge()
        self.exception_depth = 1
        self.interrupted_parked = self.parked
        self.parked = None

    def exception_return(self):
        self.pending_return = True
        self.halting = True
        self.uc.emu_stop()

    def finish_exception(self):
        sp = self.uc.reg_read(UC_ARM_REG_MSP)
        frame = struct.unpack("<8I", self.read_memory(sp, 32))
        for register, value in zip(CORE_REGISTERS, frame[:6]):
            self.uc.reg_write(register, value)
        self.uc.reg_write(UC_ARM_REG_MSP, sp + 32)
        self.uc.reg_write(UC_ARM_REG_XPSR, frame[7])
        self.uc.reg_write(UC_ARM_REG_PC, frame[6] & ~1)
        self.exception_depth = 0
        self.pending_return = False
        self.parked = self.interrupted_parked
        self.interrupted_parked = None

    def idle_until_interrupt(self):
        systick = self.systick
        if systick.enabled and systick.tick_interrupt and systick.reload:
            period = systick.reload + 1
            remaining = period - systick.elapsed() % period
            self.base_cycles += remaining
        else:
            self.base_cycles += self.core_hz // 1000

    def run(self, until_ms=None, until=None, max_instructions=None):
        self.stop_reason = None
        executed = 0
        while self.stop_reason is None:
            self.run_scheduled()
            if self.pending_return:
                self.finish_exception()
            if self.exception_depth == 0 and self.interrupt_due():
                self.enter_systick()
            if until_ms is not None and self.milliseconds() >= until_ms:
                self.stop_reason = "time limit"
                break
            if until is not None and until():
                self.stop_reason = "condition met"
                break
            if max_instructions is not None and executed >= max_instructions:
                self.stop_reason = "instruction limit"
                break
            self.step_slice()
            executed += SLICE_INSTRUCTIONS
            if self.watchdog.expired():
                self.watchdog_resets += 1
                self.stop("watchdog reset")
        return self.stop_reason

    started = False

    def next_slice_length(self):
        systick = self.systick
        waiting = systick.tick_interrupt and systick.enabled and systick.pending()
        if waiting and self.exception_depth == 0:
            return MASKED_SLICE_INSTRUCTIONS
        return SLICE_INSTRUCTIONS

    def step_slice(self):
        if not self.started:
            self.uc.reg_write(UC_ARM_REG_PC, self.entry)
            self.started = True
        start = self.uc.reg_read(UC_ARM_REG_PC)
        count = self.next_slice_length()
        self.resumed = self.parked if self.parked == start else None
        self.parked = None
        self.entered = None
        self.halting = False
        try:
            self.uc.emu_start(start | 1, 0xFFFFFFFE, count=count)
        except UcError as error:
            self.faults.append((self.cycles(), str(error), 0, self.current_pc()))
            self.stop_reason = "fault: %s at %s" % (error, self.symbol_at(self.current_pc()))
            return
        self.base_cycles += count
        if self.entered is not None and not self.halting:
            if self.uc.reg_read(UC_ARM_REG_PC) == self.entered:
                self.parked = self.entered
        if self.faults and self.stop_reason is None:
            self.stop_reason = "fault: %s" % (self.faults[-1],)
