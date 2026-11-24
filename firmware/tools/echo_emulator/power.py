import struct
from collections import Counter

from unicorn import UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_PC

from . import direct
from .core1 import CONTROL, RING, SAMPLES, SHARED_PLL_MHZ, Core1
from .core1_bridge import CONTROL_BYTES, RING_BYTES, SAMPLE_BYTES
from .machine import load_symbols


def thumb_instructions(data):
    count, offset = 0, 0
    while offset < len(data):
        word = int.from_bytes(data[offset:offset + 2], "little")
        offset += 4 if word & 0xF800 in (0xE800, 0xF000, 0xF800) else 2
        count += 1
    return count


def active_time(reads, duration_ms, tail_ms):
    intervals = [(stamp, stamp + 0.250 + 0.025 * (count - 1) + tail_ms)
                 for stamp, _, count in reads]
    total, end = 0, 0
    for first, last in sorted(intervals):
        total += max(0, min(duration_ms, last) - max(end, first, 0))
        end = max(end, last)
    return total


def on_time(events, first, last, on):
    total, previous = 0, first
    for stamp, next_on in events + [(last, False)]:
        if stamp < first:
            continue
        if stamp > last:
            break
        if on:
            total += stamp - previous
        previous, on = stamp, next_on
    return total


VOP_GATE_WORD = 0x80 + 0x7F // 16 * 4
VOP_GATE_BIT = 0x7F % 16


PROGRAMS = {0: "core1", 1: "core1_mp3", 2: "core1_m4a", 3: "core1_vorbis", 4: "core1_opus",
            5: "core1_dsd", 6: "core1_ape", 7: "core1_wma"}
SYSTICK_CTRL = 0xE000E010
SYSTICK_RELOAD = 0xE000E014
SYSTICK_CURRENT = 0xE000E018
CRU_CLOCK_SELECT = 0x40180014
SHARED_CYCLES = 0x0102005C
PLL_MHZ = 200
WFI = 0xBF30
IDLE_SEARCH_BYTES = 512
WAKES_PER_STEP = 64
WRITTEN_OFFSET = 40


class DecoderLoad:
    """Core 1 for power measurements: its program runs at the clock it sets, on its own timer.

    The program runs until its idle routine reaches WFI. Its time advances by one instruction per
    cycle of the clock it has selected (the PLL rate divided by the CRU 0x14 field). SysTick
    counts that time: reads of the current value follow it, every wrap adds a period
    to the program's elapsed counter as its tick handler would, and WFI ends at the next wrap.
    A wake runs in steps that follow core 0's time. Each time core 0 refills the input, core 1 runs
    until it catches up with core 0's clock or reaches WFI, its results reach core 0 at once, and
    it then takes a copy of core 0's control block and the input added since the last step. Core 1
    therefore works from what core 0 had shared at the previous step: it never sees a release
    before core 0 made it, and sees a slot core 0 empties during a long wake one step later. A
    publish carries only the control bytes core 1 stored since the previous step, so a slot core 0
    emptied in the meantime stays empty.
    """

    def __init__(self, audio, directory):
        self.machine = audio.machine
        self.directory = directory
        self.core = None
        self.program = None
        self.active = False
        self._native_lib = None
        self._native = None
        self._hdr = None
        self._instructions = 0
        self._wakes = 0
        self._busy_us = 0.0
        self._clock_us = Counter()
        self._next_wake_us = 0.0
        self._awake = False
        self._mhz = 100
        self._pll_mhz = PLL_MHZ
        self._divider = 2
        self._reload = 0
        self._period_start = 0.0
        self._time_us = 0.0
        self._resting = False
        self.wfi = 0
        self.counter = 0
        self.in_units = False
        self.ring_bytes = 0
        self.copied = None
        self.stored = set()
        audio.core1 = self
        self.machine.hook_symbol("core1_source_fill", self.step)

    def _detach_native(self):
        if self._native is not None:
            hdr = self._hdr
            self._instructions = hdr.instructions
            self._wakes = hdr.wakes
            self._busy_us = hdr.busy_us
            self._clock_us = Counter(self.clock_us)
            self._next_wake_us = hdr.next_wake_us
            self._awake = bool(hdr.awake)
            self._mhz = hdr.mhz
            self._pll_mhz = hdr.pll_mhz
            self._divider = hdr.divider
            self._reload = hdr.reload
            self._period_start = hdr.period_start
            self._time_us = hdr.time_us
            self._resting = bool(hdr.resting)
            self._native = None
            self._hdr = None

    @property
    def instructions(self):
        return self._hdr.instructions if self._hdr is not None else self._instructions

    @instructions.setter
    def instructions(self, value):
        if self._hdr is not None:
            self._hdr.instructions = value
        else:
            self._instructions = value

    @property
    def wakes(self):
        return self._hdr.wakes if self._hdr is not None else self._wakes

    @wakes.setter
    def wakes(self, value):
        if self._hdr is not None:
            self._hdr.wakes = value
        else:
            self._wakes = value

    @property
    def busy_us(self):
        return self._hdr.busy_us if self._hdr is not None else self._busy_us

    @busy_us.setter
    def busy_us(self, value):
        if self._hdr is not None:
            self._hdr.busy_us = value
        else:
            self._busy_us = value

    @property
    def time_us(self):
        return self._hdr.time_us if self._hdr is not None else self._time_us

    @time_us.setter
    def time_us(self, value):
        if self._hdr is not None:
            self._hdr.time_us = value
        else:
            self._time_us = value

    @property
    def period_start(self):
        return self._hdr.period_start if self._hdr is not None else self._period_start

    @period_start.setter
    def period_start(self, value):
        if self._hdr is not None:
            self._hdr.period_start = value
        else:
            self._period_start = value

    @property
    def next_wake_us(self):
        return self._hdr.next_wake_us if self._hdr is not None else self._next_wake_us

    @next_wake_us.setter
    def next_wake_us(self, value):
        if self._hdr is not None:
            self._hdr.next_wake_us = value
        else:
            self._next_wake_us = value

    @property
    def mhz(self):
        return self._hdr.mhz if self._hdr is not None else self._mhz

    @mhz.setter
    def mhz(self, value):
        if self._hdr is not None:
            self._hdr.mhz = value
        else:
            self._mhz = value

    @property
    def reload(self):
        return self._hdr.reload if self._hdr is not None else self._reload

    @reload.setter
    def reload(self, value):
        if self._hdr is not None:
            self._hdr.reload = value
        else:
            self._reload = value

    @property
    def resting(self):
        return bool(self._hdr.resting) if self._hdr is not None else self._resting

    @resting.setter
    def resting(self, value):
        if self._hdr is not None:
            self._hdr.resting = 1 if value else 0
        else:
            self._resting = bool(value)

    @property
    def awake(self):
        return bool(self._hdr.awake) if self._hdr is not None else self._awake

    @awake.setter
    def awake(self, value):
        if self._hdr is not None:
            self._hdr.awake = 1 if value else 0
        else:
            self._awake = bool(value)

    @property
    def clock_us(self):
        if self._hdr is None:
            return self._clock_us
        hdr = self._hdr
        result = Counter()
        for index in range(hdr.clock_count):
            result[hdr.clock_mhz[index]] = hdr.clock_us[index]
        return result

    def start(self, program):
        if self.active and program == self.program:
            return 1
        self._detach_native()
        self.program = program
        name = PROGRAMS[program]
        self.ring_bytes = RING_BYTES.get(program, 65536)
        self.core = Core1((self.directory / (name + ".bin")).read_bytes(), self.ring_bytes)
        symbols = load_symbols(self.directory / ("echo_" + name + ".elf"))
        if "hal_system_loop_cycles" in symbols:
            self.core.uc.mem_write(symbols["hal_system_loop_cycles"],
                                   b"\x00\x21\x01\x60\x41\x60\x70\x47")
        self.counter, self.in_units = self.elapsed_counter(symbols)
        self.wfi = self.find_wfi(symbols["core1_idle"])
        self._mhz, self._reload, self._period_start = 100, 0, 0.0
        self._pll_mhz, self._divider = PLL_MHZ, 2
        self._time_us = self.machine.microseconds()
        self._resting = False
        self._awake = False
        self.copied = None
        self.watch()
        self.active = self.core.boot()
        self.next_wake_us = self.time_us
        return int(self.active)

    @staticmethod
    def elapsed_counter(symbols):
        for prefix, units in (("elapsed_units", True), ("elapsed_cycles", False)):
            name = next((key for key in symbols if key.split(".")[0] == prefix), None)
            if name is not None:
                return symbols[name], units
        raise KeyError("core 1 has no elapsed counter")

    def find_wfi(self, idle):
        code = bytes(self.core.uc.mem_read(idle, IDLE_SEARCH_BYTES))
        for offset in range(0, IDLE_SEARCH_BYTES, 2):
            if struct.unpack_from("<H", code, offset)[0] == WFI:
                return idle + offset
        raise RuntimeError("core1_idle has no WFI")

    def watch(self):
        self._detach_native()
        if self.core._native is not None:
            lib = self.core._native_lib
            host_ram_getter = getattr(self.machine, "core1_ram_ptr", None)
            host_ram = host_ram_getter() if host_ram_getter is not None else None
            native = lib.emu_decoder_attach(
                self.core._native, self.machine.uc._uch, host_ram, self.wfi,
                self.counter, 1 if self.in_units else 0, self.ring_bytes)
            if native:
                self.core.uc._native_decoder_host = self.machine.uc
                self._native_lib = lib
                self._native = native
                hdr = lib.emu_decoder_header(native).contents
                hdr.instructions = self._instructions
                hdr.wakes = self._wakes
                hdr.busy_us = self._busy_us
                hdr.time_us = self._time_us
                hdr.period_start = self._period_start
                hdr.next_wake_us = self._next_wake_us
                hdr.mhz = self._mhz
                hdr.pll_mhz = self._pll_mhz
                hdr.divider = self._divider
                hdr.reload = self._reload
                hdr.resting = 1 if self._resting else 0
                hdr.awake = 1 if self._awake else 0
                hdr.has_copied = 0
                hdr.clock_count = 0
                for mhz, us in self._clock_us.items():
                    if hdr.clock_count < 16:
                        idx = hdr.clock_count
                        hdr.clock_mhz[idx] = mhz
                        hdr.clock_us[idx] = us
                        hdr.clock_count = idx + 1
                self._hdr = hdr
                return
        uc, cache = self.core.uc, {}

        def block(uc, address, size, user):
            if address not in cache:
                cache[address] = thumb_instructions(bytes(uc.mem_read(address, size)))
            self._instructions += cache[address]
            self.advance(cache[address] / self._mhz)

        def wfi(uc, address, size, user):
            self._resting = True
            uc.emu_stop()

        def read_current(uc, access, address, size, value, user):
            self.deliver()
            uc.mem_write(SYSTICK_CURRENT, struct.pack("<I", self.current()))

        def store(uc, access, address, size, value, user):
            self.stored.update(range(address, address + size))

        def write(uc, access, address, size, value, user):
            self.deliver()
            if address == SYSTICK_RELOAD:
                self._reload = value & 0xFFFFFF
            elif address == SYSTICK_CURRENT:
                self._period_start = self._time_us
            elif value & 0x70000000:
                self._divider = (value >> 12 & 7) + 1
                self._mhz = self._pll_mhz // self._divider

        direct.hook_add(uc, UC_HOOK_BLOCK, block)
        direct.hook_add(uc, UC_HOOK_CODE, wfi, None, self.wfi, self.wfi)
        direct.hook_add(uc, UC_HOOK_MEM_READ, read_current, None, SYSTICK_CURRENT, SYSTICK_CURRENT)
        direct.hook_add(uc, UC_HOOK_MEM_WRITE, write, None, SYSTICK_RELOAD, SYSTICK_CURRENT)
        direct.hook_add(uc, UC_HOOK_MEM_WRITE, write, None, CRU_CLOCK_SELECT, CRU_CLOCK_SELECT)
        direct.hook_add(uc, UC_HOOK_MEM_WRITE, store, None, CONTROL, CONTROL + CONTROL_BYTES - 1)

    def period_us(self):
        return (self.reload + 1) / self.mhz

    def advance(self, microseconds):
        self._time_us += microseconds
        self._clock_us[self._mhz] += microseconds

    def current(self):
        if self.reload == 0:
            return 0
        into = int((self.time_us - self.period_start) * self.mhz) % (self.reload + 1)
        return self.reload - into

    def deliver(self):
        if self._native is not None:
            self._native_lib.emu_decoder_deliver(self._native)
            return
        period = self.period_us()
        if self._reload == 0 or period <= 0:
            return
        while self._time_us - self._period_start >= period:
            self._period_start += period
            add = self._reload + 1
            if self.in_units and self._mhz != 100:
                add = add * 100 // self._mhz
            elapsed = (self.core.word(self.counter) + add) & 0xFFFFFFFF
            self.core.put(self.counter, elapsed)
            self.core.put(SHARED_CYCLES, elapsed)

    def wake_at(self):
        period = self.period_us()
        if period <= 0:
            return self.time_us
        return self.period_start + period * (int((self.time_us - self.period_start) // period) + 1)

    def run_until(self, limit_us):
        if self._native is not None:
            rc = self._native_lib.emu_decoder_run_until(self._native, float(limit_us))
            if rc != 0:
                raise RuntimeError("decoder did not reach WFI")
            return
        self._resting = False
        started = self._time_us
        for _ in range(100000):
            if self._resting or self._time_us >= limit_us:
                break
            self.core.run_slice()
        else:
            raise RuntimeError("decoder did not reach WFI")
        self._busy_us += self._time_us - started
        if self._resting:
            self.core.pc = (self.wfi + 2) | 1

    def publish(self):
        if self._native is not None:
            self._native_lib.emu_decoder_publish(self._native)
            return
        machine, core = self.machine, self.core
        if self.stored:
            machine.uc.mem_write(SAMPLES, bytes(core.uc.mem_read(SAMPLES, SAMPLE_BYTES)))
        for address in sorted(self.stored):
            machine.uc.mem_write(address, bytes(core.uc.mem_read(address, 1)))
        self.stored.clear()

    def copy_input(self):
        if self._native is not None:
            self._native_lib.emu_decoder_copy_input(self._native)
            return
        machine, core = self.machine, self.core
        control = machine.read_memory(CONTROL, CONTROL_BYTES)
        generation, written = struct.unpack_from("<I", control, 0)[0], \
            struct.unpack_from("<I", control, WRITTEN_OFFSET)[0]
        core.uc.mem_write(CONTROL, control)
        start = self.copied[1] if self.copied and self.copied[0] == generation else None
        if start is None or written < start or written - start >= self.ring_bytes:
            core.uc.mem_write(RING, machine.read_memory(RING, self.ring_bytes))
        elif written > start:
            offset = start % self.ring_bytes
            first = min(written - start, self.ring_bytes - offset)
            core.uc.mem_write(RING + offset, machine.read_memory(RING + offset, first))
            if written - start > first:
                core.uc.mem_write(RING, machine.read_memory(RING, written - start - first))
        self.copied = (generation, written)

    def wake(self):
        if self._native is not None:
            self._native_lib.emu_decoder_wake(self._native)
            return
        rest_until = self._next_wake_us
        self._clock_us[self._mhz] += rest_until - self._time_us
        self._time_us = rest_until
        self.deliver()
        self._wakes += 1
        self.stored.clear()
        self._awake = True

    def advance_to(self, now):
        if self._native is not None:
            rc = self._native_lib.emu_decoder_advance_to(self._native, float(now))
            if rc != 0:
                raise RuntimeError("decoder did not reach WFI")
            return
        self.run_until(now)
        self.deliver()
        self.publish()
        if self._resting:
            self._awake = False
            self._next_wake_us = self.wake_at()

    def sync_pll(self):
        published = self.machine.read_memory(SHARED_PLL_MHZ, 4)
        mhz = struct.unpack("<I", published)[0]
        if mhz < self._divider or mhz == self._pll_mhz:
            return
        self.deliver()
        previous = self._mhz
        self._pll_mhz = mhz
        self._mhz = mhz // self._divider
        self._period_start = self._time_us - (self._time_us - self._period_start) * previous / self._mhz
        self.core.uc.mem_write(SHARED_PLL_MHZ, published)
        if not self._awake:
            self._next_wake_us = self.wake_at()

    def step(self):
        if not self.active:
            return
        now = self.machine.microseconds()
        if self._native is not None:
            rc = self._native_lib.emu_decoder_step(self._native, float(now))
            if rc != 0:
                raise RuntimeError("decoder did not reach WFI")
            return
        self.sync_pll()
        for _ in range(WAKES_PER_STEP):
            if not self._awake:
                if now < self._next_wake_us:
                    break
                self.wake()
            self.advance_to(now)
            if self._awake:
                break
        self.copy_input()

    def figures(self, first):
        return {"busy_us": self.busy_us, "wakes": self.wakes, "instructions": self.instructions,
                "clock_us": dict(self.clock_us), "time_us": self.time_us}


class PowerMeter:
    def __init__(self, machine):
        self.machine = machine
        self.idle_us = 0
        self.wakes = 0
        self.passes = 0
        machine.hook_symbol("app_step", self.count_pass)
        self.panel_events = []
        self.backlight_events = []
        machine.stub_symbol("board_profile_wait", self.rest)
        for name, events, invert in (("panel_asleep", self.panel_events, True),
                                     ("backlight_on", self.backlight_events, False)):
            address = machine.symbols[name]
            def changed(uc, access, where, size, value, user, events=events, invert=invert):
                events.append((machine.microseconds(), bool(value) != invert))
            machine.uc.hook_add(UC_HOOK_MEM_WRITE, changed, None, address, address)

    def count_pass(self):
        self.passes += 1

    def rest(self, machine):
        before = machine.microseconds()
        machine.idle_until_interrupt()
        self.idle_us += machine.microseconds() - before
        self.wakes += 1
        machine.uc.emu_stop()

    @staticmethod
    def core1_reading(decoder):
        if decoder is None:
            return None
        return {"busy_us": decoder.busy_us, "wakes": decoder.wakes, "clock_us": Counter(decoder.clock_us),
                "time_us": decoder.time_us, "elapsed": decoder.core.word(SHARED_CYCLES),
                "idle": decoder.core.word(SHARED_CYCLES - 4)}

    def core1_figures(self, decoder, first, elapsed_us):
        last = self.core1_reading(decoder)
        if last is None:
            return {"core1_busy_percent": 0, "core1_wakes_per_s": 0, "core1_clock_ms": {},
                    "core1_reported_idle_percent": None}
        span = last["time_us"] - first["time_us"]
        clocks = Counter(last["clock_us"])
        clocks.subtract(first["clock_us"])
        counted = (last["elapsed"] - first["elapsed"]) & 0xFFFFFFFF
        idle = (last["idle"] - first["idle"]) & 0xFFFFFFFF
        return {
            "core1_busy_percent": round(100 * (last["busy_us"] - first["busy_us"]) / span, 3) if span else 0,
            "core1_wakes_per_s": round((last["wakes"] - first["wakes"]) * 1000000 / elapsed_us, 3),
            "core1_clock_ms": {str(mhz): round(us / 1000, 3) for mhz, us in sorted(clocks.items()) if us > 0},
            "core1_reported_idle_percent": round(100 * idle / counted, 3) if counted else None,
        }

    def measure(self, rig, card, duration_ms):
        machine, decoder = self.machine, rig.audio.core1
        decoder = decoder if isinstance(decoder, DecoderLoad) else None
        first, idle, wakes, passes = machine.microseconds(), self.idle_us, self.wakes, self.passes
        instructions = decoder.instructions if decoder else 0
        core1_first = self.core1_reading(decoder)
        clock_first, clock_index = machine.core_hz, len(machine.clock_log)
        panel_on = not machine.read_memory(machine.symbols["panel_asleep"], 1)[0]
        backlight_on = bool(machine.read_memory(machine.symbols["backlight_on"], 1)[0])
        rig.run_for(duration_ms)
        elapsed = machine.microseconds() - first
        reads = [(stamp - first / 1000, lba, count) for stamp, lba, count in card.read_log
                 if first / 1000 <= stamp < (first + elapsed) / 1000]
        clocks, last, hz = Counter(), first / 1000, clock_first
        for stamp, next_hz in machine.clock_log[clock_index:] + [((first + elapsed) / 1000, 0)]:
            clocks[hz] += stamp - last
            hz, last = next_hz, stamp
        cycles = decoder.instructions - instructions if decoder else 0
        gaps = [b[0] - a[0] for a, b in zip(reads, reads[1:])]
        return {
            "duration_ms": elapsed / 1000,
            "core0_busy_percent": round(100 * (1 - (self.idle_us - idle) / elapsed), 3),
            "core0_wfi_wakes_per_s": round((self.wakes - wakes) * 1000000 / elapsed, 3),
            "core0_passes_per_s": round((self.passes - passes) * 1000000 / elapsed, 3),
            "core0_clock_ms": {str(rate): round(ms, 3) for rate, ms in sorted(clocks.items())},
            "core1_instructions": cycles,
            **self.core1_figures(decoder, core1_first, elapsed),
            "reads": len(reads), "read_size_bytes": dict(Counter(count * 512 for _, _, count in reads)),
            "max_read_gap_ms": round(max(gaps, default=0), 3),
            "transfer_ms_per_minute": round(sum(0.250 + 0.025 * (count - 1)
                                                 for _, _, count in reads) * 60000000 / elapsed, 3),
            "active_ms_per_minute_tail_5ms": round(active_time(reads, elapsed / 1000, 5) * 60000000 / elapsed, 3),
            "active_ms_per_minute_tail_50ms": round(active_time(reads, elapsed / 1000, 50) * 60000000 / elapsed, 3),
            "panel_asleep": bool(machine.read_memory(machine.symbols["panel_asleep"], 1)[0]),
            "vop_clock_gated": bool(machine.cru.regs.get(VOP_GATE_WORD, 0) >> VOP_GATE_BIT & 1),
            "backlight_on": bool(machine.read_memory(machine.symbols["backlight_on"], 1)[0]),
            "panel_on_ms": on_time(self.panel_events, first, first + elapsed, panel_on) / 1000,
            "backlight_on_ms": on_time(self.backlight_events, first, first + elapsed, backlight_on) / 1000,
            "underruns": len(rig.audio.underruns(int(first / 1000))),
            "output_underruns": rig.audio.output_underruns(),
        }
