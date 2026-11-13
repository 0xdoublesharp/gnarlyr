import struct
from collections import Counter

from unicorn import UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_PC

from .core1 import CONTROL, RING, SAMPLES, Core1
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


class DecoderLoad:
    """Core 1 for power measurements: its program runs at the clock it sets, on its own timer.

    The program runs until its idle routine reaches WFI. Its time advances by one instruction per
    cycle of the clock it has selected (CRU 0x14 bits 14:12 under the write mask, from the 200 MHz
    PLL). SysTick counts that time: reads of the current value follow it, every wrap adds a period
    to the program's elapsed counter as its tick handler would, and WFI ends at the next wrap.
    Decoded slots reach core 0 only once the time spent decoding them has passed. A publish carries
    only the control bytes core 1 stored during that wake, as its stores would on the chip, so a slot
    core 0 emptied in the meantime stays empty.
    """

    def __init__(self, audio, directory):
        self.machine = audio.machine
        self.directory = directory
        self.core = None
        self.active = False
        self.instructions = 0
        self.wakes = 0
        self.busy_us = 0.0
        self.clock_us = Counter()
        self.next_wake_us = 0.0
        self.publish_at_us = None
        self.ring_bytes = 0
        self.stored = set()
        audio.core1 = self
        self.machine.hook_symbol("core1_source_fill", self.step)

    def start(self, program):
        if self.active:
            return 1
        name = PROGRAMS[program]
        self.ring_bytes = RING_BYTES.get(program, 65536)
        self.core = Core1((self.directory / (name + ".bin")).read_bytes(), self.ring_bytes)
        symbols = load_symbols(self.directory / ("echo_" + name + ".elf"))
        if "hal_system_loop_cycles" in symbols:
            self.core.uc.mem_write(symbols["hal_system_loop_cycles"], b"\x00\x21\x01\x60\x41\x60\x70\x47")
        self.counter, self.in_units = self.elapsed_counter(symbols)
        self.wfi = self.find_wfi(symbols["core1_idle"])
        self.mhz, self.reload, self.period_start = 100, 0, 0.0
        self.time_us = self.machine.microseconds()
        self.resting = False
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
        uc, cache = self.core.uc, {}

        def block(uc, address, size, user):
            if address not in cache:
                cache[address] = thumb_instructions(bytes(uc.mem_read(address, size)))
            self.instructions += cache[address]
            self.advance(cache[address] / self.mhz)

        def wfi(uc, address, size, user):
            self.resting = True
            uc.emu_stop()

        def read_current(uc, access, address, size, value, user):
            self.deliver()
            uc.mem_write(SYSTICK_CURRENT, struct.pack("<I", self.current()))

        def store(uc, access, address, size, value, user):
            self.stored.update(range(address, address + size))

        def write(uc, access, address, size, value, user):
            self.deliver()
            if address == SYSTICK_RELOAD:
                self.reload = value & 0xFFFFFF
            elif address == SYSTICK_CURRENT:
                self.period_start = self.time_us
            elif value & 0x70000000:
                self.mhz = PLL_MHZ // ((value >> 12 & 7) + 1)

        uc.hook_add(UC_HOOK_BLOCK, block)
        uc.hook_add(UC_HOOK_CODE, wfi, None, self.wfi, self.wfi)
        uc.hook_add(UC_HOOK_MEM_READ, read_current, None, SYSTICK_CURRENT, SYSTICK_CURRENT)
        uc.hook_add(UC_HOOK_MEM_WRITE, write, None, SYSTICK_RELOAD, SYSTICK_CURRENT)
        uc.hook_add(UC_HOOK_MEM_WRITE, write, None, CRU_CLOCK_SELECT, CRU_CLOCK_SELECT)
        uc.hook_add(UC_HOOK_MEM_WRITE, store, None, CONTROL, CONTROL + CONTROL_BYTES - 1)

    def period_us(self):
        return (self.reload + 1) / self.mhz

    def advance(self, microseconds):
        self.time_us += microseconds
        self.clock_us[self.mhz] += microseconds

    def current(self):
        if self.reload == 0:
            return 0
        into = int((self.time_us - self.period_start) * self.mhz) % (self.reload + 1)
        return self.reload - into

    def deliver(self):
        period = self.period_us()
        if self.reload == 0 or period <= 0:
            return
        while self.time_us - self.period_start >= period:
            self.period_start += period
            add = self.reload + 1
            if self.in_units and self.mhz != 100:
                add = add * 100 // self.mhz
            elapsed = (self.core.word(self.counter) + add) & 0xFFFFFFFF
            self.core.put(self.counter, elapsed)
            self.core.put(SHARED_CYCLES, elapsed)

    def wake_at(self):
        period = self.period_us()
        if period <= 0:
            return self.time_us
        return self.period_start + period * (int((self.time_us - self.period_start) // period) + 1)

    def run_until_rest(self):
        self.resting = False
        started = self.time_us
        for _ in range(100000):
            self.core.run_slice()
            if self.resting:
                break
        else:
            raise RuntimeError("decoder did not reach WFI")
        self.busy_us += self.time_us - started
        self.core.pc = (self.wfi + 2) | 1

    def publish(self):
        machine, core = self.machine, self.core
        machine.uc.mem_write(SAMPLES, bytes(core.uc.mem_read(SAMPLES, SAMPLE_BYTES)))
        for address in sorted(self.stored):
            machine.uc.mem_write(address, bytes(core.uc.mem_read(address, 1)))
        self.stored.clear()

    def wake(self):
        machine, core = self.machine, self.core
        rest_until = self.next_wake_us
        self.clock_us[self.mhz] += rest_until - self.time_us
        self.time_us = rest_until
        self.deliver()
        self.wakes += 1
        core.uc.mem_write(CONTROL, machine.read_memory(CONTROL, CONTROL_BYTES))
        core.uc.mem_write(RING, machine.read_memory(RING, self.ring_bytes))
        self.stored.clear()
        self.run_until_rest()
        self.deliver()
        self.publish_at_us = self.time_us
        self.next_wake_us = self.wake_at()

    def step(self):
        if not self.active:
            return
        now = self.machine.microseconds()
        for _ in range(WAKES_PER_STEP):
            if self.publish_at_us is not None:
                if now < self.publish_at_us:
                    return
                self.publish()
                self.publish_at_us = None
            if now < self.next_wake_us:
                return
            self.wake()

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
