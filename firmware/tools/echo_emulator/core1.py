import struct

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP

CODE_BASE = 0x0
CODE_BYTES = 0x20000
RAM_BASE = 0x01000000
RAM_BYTES = 0x60000
SHARED = 0x01020000
CONTROL = 0x01020100
SAMPLES = 0x0102E000
RING = 0x01040000
HEAP = 0x01050000
BLOCK = 4608
READY_MAGIC = 0x45434831
COMMAND_STREAM = 2
SLICE = 1000
SLOT_FULL = 1
RESULT_READY = 0
RESULT_END = 2
SLOT_BYTES = 20
CONTROL_SLOTS = 52
OFFSETS = {"generation": 0, "channels": 8, "bits": 12, "first_sample": 28, "start": 32,
           "end": 36, "written": 40, "acked": 44, "released": 48,
           "tail_trim": CONTROL_SLOTS + 2 * SLOT_BYTES}
VIRTUAL_END = 0x7FFFFFFF
CLOCK_100_MHZ = 0x70001000
CLOCK_200_MHZ = 0x70000000


class Core1:
    def __init__(self, program, ring_bytes):
        self.ring_bytes = ring_bytes
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.mem_map(CODE_BASE, CODE_BYTES)
        self.uc.mem_map(RAM_BASE, RAM_BYTES)
        self.uc.mem_map(0xE0000000, 0x100000)
        self.uc.mem_map(0x40000000, 0x200000)
        self.clock_writes = []
        self.clock = CLOCK_100_MHZ
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.note_clock, begin=0x40180014, end=0x40180014)
        self.uc.mem_write(CODE_BASE, program)
        self.instructions = 0
        self.next_slot = 0
        sp, pc = struct.unpack("<II", program[:8])
        self.uc.reg_write(UC_ARM_REG_SP, sp)
        self.pc = pc | 1

    def note_clock(self, uc, access, address, size, value, data):
        self.clock_writes.append(value)
        self.clock = value

    def word(self, address):
        return struct.unpack("<I", self.uc.mem_read(address, 4))[0]

    def put(self, address, value):
        self.uc.mem_write(address, struct.pack("<I", value))

    def control(self, name):
        return self.word(CONTROL + OFFSETS[name])

    def set_control(self, name, value):
        self.put(CONTROL + OFFSETS[name], value)

    def run_slice(self):
        self.uc.emu_start(self.pc, 0xFFFFFFFE, count=SLICE)
        self.pc = self.uc.reg_read(UC_ARM_REG_PC) | 1
        self.instructions += SLICE

    def boot(self):
        self.put(SHARED + 0x0C, COMMAND_STREAM)
        for _ in range(2000):
            self.run_slice()
            if self.word(SHARED) == READY_MAGIC:
                return True
        return False

    def start_stream(self, channels, bits, first_sample):
        self.set_control("channels", channels)
        self.set_control("bits", bits)
        self.set_control("first_sample", first_sample)
        self.set_control("start", 0)
        self.set_control("end", VIRTUAL_END)
        self.set_control("written", 0)
        self.set_control("generation", 1)

    def feed(self, stream, fed):
        limit = min(self.control("released") + self.ring_bytes, len(stream))
        while fed < limit:
            position = fed % self.ring_bytes
            count = min(limit - fed, self.ring_bytes - position)
            self.uc.mem_write(RING + position, stream[fed:fed + count])
            fed += count
        self.set_control("written", fed)
        return fed

    def take_slots(self, channels):
        blocks = []
        for _ in range(2):
            slot = self.next_slot
            at = CONTROL + CONTROL_SLOTS + SLOT_BYTES * slot
            if self.word(at) != SLOT_FULL:
                break
            self.next_slot = 1 - slot
            result, first, count = self.word(at + 8), self.word(at + 12), self.word(at + 16)
            channel_data = []
            if result == RESULT_READY:
                for channel in range(channels):
                    base = SAMPLES + 4 * BLOCK * (slot * 2 + channel)
                    channel_data.append(struct.unpack(f"<{count}i", self.uc.mem_read(base, 4 * count)))
            blocks.append((result, first, count, channel_data, self.instructions))
            self.put(at, 0)
        return blocks


def decode_stream(program, stream, channels, bits, first_sample, ring_bytes=0x10000):
    core = Core1(program, ring_bytes)
    if not core.boot():
        raise RuntimeError("core 1 never reported ready")
    core.start_stream(channels, bits, first_sample)
    fed = 0
    blocks = []
    for _ in range(2_000_000):
        fed = core.feed(stream + bytes(ring_bytes), fed)
        core.run_slice()
        for block in core.take_slots(channels):
            blocks.append(block)
            if block[0] != RESULT_READY:
                return blocks
    raise RuntimeError("the stream never ended")
