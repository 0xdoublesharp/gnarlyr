from .core1 import CONTROL, RING, SAMPLES, Core1

CONTROL_BYTES = 124
CORE1_VORBIS = 3
CORE1_OPUS = 4
CORE1_APE = 5
RING_BYTES = {CORE1_VORBIS: 0x10000, CORE1_OPUS: 40960, CORE1_APE: 65536}
SAMPLE_BYTES = RING - SAMPLES
DECODER_OWNED = ((44, 48), (96, 24))


class Core1Bridge:
    def __init__(self, audio, programs):
        self.machine = audio.machine
        self.programs = programs
        self.ring_bytes = 0
        self.active = False
        self.core = None
        self.starts = 0
        self.started = []
        audio.core1 = self
        self.machine.hook_symbol("core1_source_fill", self.step)

    def start(self, program):
        self.active = False
        if program not in self.programs:
            return 0
        self.ring_bytes = RING_BYTES[program]
        self.core = Core1(self.programs[program], self.ring_bytes)
        self.active = self.core.boot()
        self.starts += int(self.active)
        if self.active:
            self.started.append(program)
        return int(self.active)

    def step(self):
        if not self.active:
            return
        machine, core = self.machine, self.core
        core.uc.mem_write(CONTROL, machine.read_memory(CONTROL, CONTROL_BYTES))
        core.uc.mem_write(RING, machine.read_memory(RING, self.ring_bytes))
        for _ in range(32):
            core.run_slice()
        machine.uc.mem_write(SAMPLES, bytes(core.uc.mem_read(SAMPLES, SAMPLE_BYTES)))
        for offset, count in DECODER_OWNED:
            machine.uc.mem_write(CONTROL + offset, bytes(core.uc.mem_read(CONTROL + offset, count)))
