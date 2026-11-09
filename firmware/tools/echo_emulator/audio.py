import struct

from unicorn import UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1

PRODUCED_OFFSET = 32
ITEM_BYTES = 24
ITEM_NEXT = 8
ITEM_WORDS = 16
WORD_BYTES = 4
WORDS_PER_FRAME = 2
RING_BYTES = 7 * 4000 * WORD_BYTES
SILENT_RETURNS = ("dac_set_muted", "dac_output_on", "dac_set_gain", "dac_path_start_transfer",
                  "dac_path_stop_transfer", "dac_path_stop", "dmac_suspend", "dmac_resume",
                  "board_core1_stop")
FALSE_RETURNS = ("dac_path_take_underrun", "board_core1_start_stream", "board_core1_streaming")


class AudioPath:
    def __init__(self, machine):
        self.machine = machine
        self.rate = 44100
        self.ring = machine.symbols["music_ring"]
        self.running = False
        self.started_us = 0
        self.items = 0
        self.plays = []
        self.audible = []
        self.levels = []
        self.engine = machine.symbols["engine"]
        self.install()

    def install(self):
        machine = self.machine
        self.volumes = []
        machine.stub_symbol("dac_path_start", self.start)
        machine.stub_symbol("dac_set_volume", self.set_volume)
        machine.stub_symbol("dmac_start_loop", self.start_loop)
        machine.stub_symbol("dmac_source", self.source)
        machine.stub_symbol("dmac_running", lambda machine: int(self.running))
        for name in SILENT_RETURNS:
            machine.stub_symbol(name, lambda machine: None)
        for name in FALSE_RETURNS:
            machine.stub_symbol(name, lambda machine: 0)
        machine.stub_symbol("dmac_stop", self.stop)
        machine.hook_symbol("hal_audio_play", self.note_play)
        machine.hook_symbol("audio_engine_pump", self.sample_level)
        machine.uc.hook_add(UC_HOOK_MEM_WRITE, self.ring_write, None, self.ring,
                            self.ring + RING_BYTES - 1)

    def set_volume(self, machine):
        self.volumes.append((machine.milliseconds(), machine.reg(UC_ARM_REG_R1)))

    def start(self, machine):
        self.rate = machine.reg(UC_ARM_REG_R0)
        return 0

    def start_loop(self, machine):
        self.items = machine.reg(UC_ARM_REG_R0)
        self.started_us = machine.microseconds()
        self.running = True
        return 1

    def stop(self, machine):
        self.running = False

    def ring_words(self):
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
        elapsed = machine.microseconds() - self.started_us
        words = elapsed * self.rate * WORDS_PER_FRAME // 1000000
        return self.ring + (words % self.ring_words()) * WORD_BYTES

    def sample_level(self):
        if not self.running:
            return
        produced = struct.unpack("<I", self.machine.read_memory(self.engine + PRODUCED_OFFSET, 4))[0]
        elapsed = self.machine.microseconds() - self.started_us
        self.levels.append((self.machine.milliseconds(), produced - elapsed * self.rate // 1000000))

    def underruns(self, since_ms=0):
        return [stamp for stamp, level in self.levels if stamp >= since_ms and level < 0]

    def note_play(self):
        self.plays.append(self.machine.milliseconds())

    def ring_write(self, uc, access, address, size, value, user):
        if not value or not self.plays:
            return
        if not self.audible or self.audible[-1] < self.plays[-1]:
            self.audible.append(self.machine.milliseconds())
