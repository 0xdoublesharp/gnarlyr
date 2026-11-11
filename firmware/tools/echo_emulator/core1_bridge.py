from .core1 import CONTROL, RING, SAMPLES, Core1

CONTROL_BYTES = 124
CORE1_VORBIS = 3
RING_BYTES = 0x10000
SAMPLE_BYTES = RING - SAMPLES
DECODER_OWNED = ((44, 48), (96, 24))


class Core1Bridge:
    def __init__(self, audio, program):
        self.machine = audio.machine
        self.program = program
        self.active = False
        self.core = None
        self.starts = 0
        audio.core1 = self
        self.machine.hook_symbol("core1_source_fill", self.step)

    def start(self, program):
        self.active = False
        if program != CORE1_VORBIS:
            return 0
        self.core = Core1(self.program, RING_BYTES)
        self.active = self.core.boot()
        self.starts += int(self.active)
        return int(self.active)

    def step(self):
        if not self.active:
            return
        machine, core = self.machine, self.core
        core.uc.mem_write(CONTROL, machine.read_memory(CONTROL, CONTROL_BYTES))
        core.uc.mem_write(RING, machine.read_memory(RING, RING_BYTES))
        for _ in range(32):
            core.run_slice()
        machine.uc.mem_write(SAMPLES, bytes(core.uc.mem_read(SAMPLES, SAMPLE_BYTES)))
        for offset, count in DECODER_OWNED:
            machine.uc.mem_write(CONTROL + offset, bytes(core.uc.mem_read(CONTROL + offset, count)))
