from .core1 import CONTROL, RING, SAMPLES, SHARED_PLL_MHZ, SLICE, Core1

CONTROL_BYTES = 128
CORE1_FLAC = 0
CORE1_MP3 = 1
CORE1_M4A = 2
CORE1_VORBIS = 3
CORE1_OPUS = 4
CORE1_DSD = 5
CORE1_APE = 6
CORE1_WMA = 7
RING_BYTES = {CORE1_FLAC: 0x20000, CORE1_MP3: 0x10000, CORE1_M4A: 0x10000, CORE1_VORBIS: 0x10000,
              CORE1_OPUS: 40960, CORE1_DSD: 32768, CORE1_APE: 65536, CORE1_WMA: 8192}
SAMPLE_BYTES = RING - SAMPLES
DECODER_OWNED = ((44, 48), (96, 24))
SLICES_PER_FILL = 32


class Core1Bridge:
    def __init__(self, audio, programs, instructions_per_us=0):
        self.machine = audio.machine
        self.instructions_per_us = instructions_per_us
        self.paced_us = 0
        self.owed = 0
        self.programs = programs
        self.ring_bytes = 0
        self.active = False
        self.core = None
        self.starts = 0
        self.started = []
        self.slices = SLICES_PER_FILL
        audio.core1 = self
        self.machine.hook_symbol("core1_source_fill", self.step)

    def start(self, program):
        self.active = False
        if program not in self.programs:
            return 0
        self.ring_bytes = RING_BYTES[program]
        self.core = Core1(self.machine.read_memory(0x01000000, len(self.programs[program])),
                          self.ring_bytes, shared_with=self.machine)
        self.active = self.core.boot()
        self.starts += int(self.active)
        if self.active:
            self.started.append(program)
            self.paced_us = self.machine.microseconds()
            self.owed = 0
        return int(self.active)

    def step(self):
        if not self.active:
            return
        machine, core = self.machine, self.core
        due = self.slices_due()
        if core._shared:
            if due:
                core.run_budget(due * SLICE)
            return
        core.uc.mem_write(CONTROL, machine.read_memory(CONTROL, CONTROL_BYTES))
        core.uc.mem_write(SHARED_PLL_MHZ, machine.read_memory(SHARED_PLL_MHZ, 4))
        core.uc.mem_write(RING, machine.read_memory(RING, self.ring_bytes))
        if due:
            core.run_budget(due * SLICE)
        machine.uc.mem_write(SAMPLES, bytes(core.uc.mem_read(SAMPLES, SAMPLE_BYTES)))
        for offset, count in DECODER_OWNED:
            machine.uc.mem_write(CONTROL + offset, bytes(core.uc.mem_read(CONTROL + offset, count)))

    def slices_due(self):
        if not self.instructions_per_us:
            return self.slices
        now = self.machine.microseconds()
        self.owed += (now - self.paced_us) * self.instructions_per_us
        self.paced_us = now
        slices = self.owed // SLICE
        self.owed -= slices * SLICE
        return slices
