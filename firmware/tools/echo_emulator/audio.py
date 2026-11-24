import struct

import make_image

from . import direct

from unicorn import UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1

PRODUCED_OFFSET = 48
ITEM_BYTES = 24
ITEM_NEXT = 8
ITEM_WORDS = 16
WORD_BYTES = 4
WORDS_PER_FRAME = 2
RING_BYTES = 7 * 4000 * WORD_BYTES
SILENT_RETURNS = ("dac_set_muted", "dac_output_on", "dac_path_start_transfer", "dac_path_stop_transfer",
                  "dac_path_stop")


class AudioPath:
    def __init__(self, machine, *, real_dac=False, real_dmac=False):
        self.machine = machine
        self.real_dac = real_dac
        self.real_dmac_enabled = real_dmac
        self.dac_bus = None
        self.real_dmac = None
        self.core1 = None
        self.rate = 44100
        self.ring = machine.symbols["music_ring"]
        self.dma_ring = self.ring
        self._running = False
        self._suspend_depth = 0
        self.paused_us = 0
        self.output_fault = False
        self.resume_fault = False
        self.start_fault = False
        self.mute_fault = False
        self.analog_fault = False
        self.dac_events = []
        self.started_us = 0
        self.items = 0
        self.plays = []
        self.audible = []
        self.levels = []
        self.engine = machine.symbols["engine"]
        self.requested_program = 0
        self.install()
        from .core1_bridge import CORE1_FLAC, Core1Bridge
        offset = make_image.CORE1_FLAC_OFFSET
        size = struct.unpack_from("<I", machine.image, offset + 4)[0]
        begin = offset + make_image.CORE1_MODULE_HEADER_SIZE
        Core1Bridge(self, {CORE1_FLAC: machine.image[begin:begin + size]})

    @property
    def running(self):
        return self.real_dmac.running if self.real_dmac is not None else self._running

    @running.setter
    def running(self, value):
        self._running = value

    @property
    def suspend_depth(self):
        if self.real_dmac is not None:
            return 1 if self.real_dmac.suspended else 0
        return self._suspend_depth

    @suspend_depth.setter
    def suspend_depth(self, value):
        self._suspend_depth = value

    def install(self):
        machine = self.machine
        self.volumes = []
        if self.real_dac:
            from .dac_hardware import RealCs43198Bus, RealI2sRegs
            from .devices import RegisterFile
            self.dac_bus = RealCs43198Bus(machine)
            machine.map_device(self.dac_bus, 0x40040000, 0x1000)
            machine.map_device(RealI2sRegs(), 0x40030000, 0x1000)
            machine.map_device(RegisterFile(), 0x40090000, 0x1000)
            machine.hook_symbol("dac_path_start", self.note_rate)
            machine.hook_symbol("dac_set_volume", self.note_volume)
            machine.hook_symbol("dac_apply_analog", lambda: self.dac_events.append("apply"))
        else:
            machine.stub_symbol("dac_path_start", self.start)
            machine.stub_symbol("dac_set_volume", self.set_volume)
            for name in SILENT_RETURNS:
                machine.stub_symbol(name, lambda machine: None)
            machine.stub_symbol("dac_apply_analog", self.apply_analog)
            machine.stub_symbol("dac_path_take_underrun", self.take_output_fault)
        if self.real_dmac_enabled:
            from .dac_hardware import RealDmac
            self.real_dmac = RealDmac(machine, lambda: self.rate)
            self.real_dmac.attach()
        else:
            machine.stub_symbol("dmac_start_loop", self.start_loop)
            machine.stub_symbol("dmac_source", self.source)
            machine.stub_symbol("dmac_running", lambda machine: int(self.running))
            machine.stub_symbol("dmac_stop", self.stop)
            machine.stub_symbol("dmac_suspend", self.suspend)
            machine.stub_symbol("dmac_resume", self.resume)
            machine.stub_symbol("dmac_suspended", lambda machine: int(self.suspend_depth > 0))
        machine.hook_symbol("board_core1_start_stream", self.note_program)
        for name in ("core1_power_up", "core1_hold", "core1_prepare"):
            machine.stub_symbol(name, lambda machine: 0)
        machine.stub_symbol("core1_run", self.start_core1)
        machine.stub_symbol("core1_stop", self.stop_core1)
        machine.hook_symbol("dac_path_start_transfer", self.note_start)
        machine.hook_symbol("dac_set_muted", self.note_mute)
        machine.hook_symbol("hal_audio_play", self.note_play)
        machine.hook_symbol("audio_engine_pump", self.sample_level)
        direct.hook_add(machine.uc, UC_HOOK_MEM_WRITE, self.ring_write, None, self.ring,
                        self.ring + RING_BYTES - 1)

    def note_rate(self):
        self.rate = self.machine.reg(UC_ARM_REG_R0)

    def note_volume(self):
        self.volumes.append((self.machine.milliseconds(), self.machine.reg(UC_ARM_REG_R1)))

    def note_program(self):
        self.requested_program = self.machine.reg(UC_ARM_REG_R0)

    def start_core1(self, machine):
        command = struct.unpack("<I", machine.read_memory(0x0102000C, 4))[0]
        if command != 2:
            return 0
        return 0 if self.core1 is not None and self.core1.start(self.requested_program) else 1

    def stop_core1(self, machine):
        if self.core1 is not None:
            self.core1.active = False
        return 0

    def set_volume(self, machine):
        self.volumes.append((machine.milliseconds(), machine.reg(UC_ARM_REG_R1)))
        return 1

    def start(self, machine):
        self.rate = machine.reg(UC_ARM_REG_R0)
        return 0

    def start_loop(self, machine):
        self.items = machine.reg(UC_ARM_REG_R0)
        self.dma_ring = struct.unpack("<I", machine.read_memory(self.items, 4))[0]
        self.started_us = machine.microseconds()
        self.running = True
        self.suspend_depth = 0
        return 1

    def stop(self, machine):
        self.running = False
        self.suspend_depth = 0

    def suspend(self, machine):
        if self.running:
            if self.suspend_depth == 0:
                self.paused_us = machine.microseconds()
            self.suspend_depth += 1

    def resume(self, machine):
        if self.suspend_depth:
            self.suspend_depth -= 1
            if self.suspend_depth == 0:
                self.started_us += machine.microseconds() - self.paused_us
                self.output_fault |= self.resume_fault
                self.resume_fault = False

    def note_start(self):
        self.output_fault |= self.start_fault
        self.start_fault = False

    def apply_analog(self, machine):
        self.dac_events.append("apply")
        return 0 if self.analog_fault else 1

    def note_mute(self):
        self.dac_events.append("mute" if self.machine.reg(UC_ARM_REG_R1) else "unmute")
        if self.machine.reg(UC_ARM_REG_R1):
            self.output_fault |= self.mute_fault
            self.mute_fault = False

    def take_output_fault(self, machine):
        latched = self.output_fault
        self.output_fault = False
        return int(latched)

    def output_underruns(self):
        return struct.unpack("<I", self.machine.read_memory(self.machine.symbols["stats"] + 8, 4))[0]

    def elapsed_us(self, machine):
        if self.real_dmac is not None:
            return self.real_dmac.elapsed_us()
        now = self.paused_us if self.suspend_depth else machine.microseconds()
        return now - self.started_us

    def ring_words(self):
        if self.real_dmac is not None:
            return self.real_dmac.ring_words()
        total = 0
        item = self.items
        for _ in range(64):
            words = struct.unpack_from("<I", self.machine.read_memory(item, ITEM_BYTES), ITEM_WORDS)[0]
            total += words
            item = struct.unpack_from("<I", self.machine.read_memory(item, ITEM_BYTES), ITEM_NEXT)[0]
            if item == self.items:
                break
        return total

    def source(self, machine):
        if not self.running:
            return 0
        elapsed = self.elapsed_us(machine)
        words = elapsed * self.rate * WORDS_PER_FRAME // 1000000
        return self.dma_ring + (words % self.ring_words()) * WORD_BYTES

    def sample_level(self):
        if not self.running or self.suspend_depth:
            return
        produced = struct.unpack("<I", self.machine.read_memory(self.engine + PRODUCED_OFFSET, 4))[0]
        elapsed = self.elapsed_us(self.machine)
        self.levels.append((self.machine.milliseconds(), produced - elapsed * self.rate // 1000000))

    def underruns(self, since_ms=0):
        return [stamp for stamp, level in self.levels if stamp >= since_ms and level < 0]

    def note_play(self):
        self.plays.append(self.machine.milliseconds())

    def ring_write(self, uc, access, address, size, value, user):
        if not value or not self.plays:
            return
        if self.audible and self.audible[-1] >= self.plays[-1]:
            return
        if self.running and address >= self.ring + self.ring_words() * WORD_BYTES:
            return
        self.audible.append(self.machine.milliseconds())
