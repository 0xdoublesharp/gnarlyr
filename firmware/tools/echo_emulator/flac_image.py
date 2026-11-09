import math
import struct

MAGIC = b"fLaC"
STREAMINFO_BYTES = 34
HEADER_BYTES = 4 + 4 + STREAMINFO_BYTES
BLOCK_SAMPLES = 4096
CHANNELS = 2
SAMPLE_RATE = 44100
BITS = 16
HEADER_FIXED = 8
CODED_AND_RATE_BYTES = 3
FRAME_BYTES = HEADER_FIXED + CHANNELS * (1 + BLOCK_SAMPLES * BITS // 8) + 2
CRC16_POLY = 0x8005
CRC8_POLY = 0x07
RATE_CODES = {1: 0, 2: 12, 3: 13}


def crc_table(poly, width):
    top = 1 << (width - 1)
    mask = (1 << width) - 1
    table = []
    for byte in range(256):
        value = byte << (width - 8)
        for _ in range(8):
            value = ((value << 1) ^ poly if value & top else value << 1) & mask
        table.append(value)
    return table


CRC16 = crc_table(CRC16_POLY, 16)
CRC8 = crc_table(CRC8_POLY, 8)


def crc16(data, state=0):
    for byte in data:
        state = ((state << 8) & 0xFFFF) ^ CRC16[(state >> 8) ^ byte]
    return state


def crc8(data):
    state = 0
    for byte in data:
        state = CRC8[state ^ byte]
    return state


def utf8_number(value):
    if value < 0x80:
        return bytes([value])
    if value < 0x800:
        return bytes([0xC0 | value >> 6, 0x80 | value & 0x3F])
    return bytes([0xE0 | value >> 12, 0x80 | value >> 6 & 0x3F, 0x80 | value & 0x3F])


def body_bytes():
    samples = bytearray()
    for index in range(BLOCK_SAMPLES):
        value = int(9000 * math.sin(2 * math.pi * 440 * index / SAMPLE_RATE))
        samples += struct.pack(">h", value)
    subframe = bytes([0x02]) + bytes(samples)
    return subframe * CHANNELS


def streaminfo(total_samples):
    packed = SAMPLE_RATE << 44 | (CHANNELS - 1) << 41 | (BITS - 1) << 36 | total_samples
    return (struct.pack(">HHI", BLOCK_SAMPLES, BLOCK_SAMPLES, FRAME_BYTES)[:4]
            + FRAME_BYTES.to_bytes(3, "big") + FRAME_BYTES.to_bytes(3, "big")
            + packed.to_bytes(8, "big") + bytes(16))


class FlacImage:
    def __init__(self, frames):
        self.frames = frames
        self.size = HEADER_BYTES + frames * FRAME_BYTES
        self.seconds = frames * BLOCK_SAMPLES // SAMPLE_RATE
        self.body = body_bytes()
        self.basis = self.crc_basis()
        self.head = (MAGIC + bytes([0x80, 0, 0, STREAMINFO_BYTES])
                     + streaminfo(frames * BLOCK_SAMPLES))
        self.last = (None, None)

    def crc_basis(self):
        zero = crc16(self.body)
        return zero, [crc16(self.body, 1 << bit) ^ zero for bit in range(16)]

    def frame(self, number):
        if self.last[0] == number:
            return self.last[1]
        coded = utf8_number(number)
        extra = CODED_AND_RATE_BYTES - len(coded)
        rate_code = RATE_CODES[1 + extra]
        header = bytes([0xFF, 0xF8, 0xC0 | rate_code, 0x10]) + coded + bytes(extra)
        header += bytes([crc8(header)])
        state = crc16(header)
        zero, basis = self.basis
        final = zero
        for bit in range(16):
            if state >> bit & 1:
                final ^= basis[bit]
        data = header + self.body + struct.pack(">H", final)
        self.last = (number, data)
        return data

    def read(self, offset, length):
        out = bytearray()
        end = min(offset + length, self.size)
        while offset < end:
            if offset < HEADER_BYTES:
                chunk = self.head[offset:end]
                offset += len(chunk)
            else:
                number, within = divmod(offset - HEADER_BYTES, FRAME_BYTES)
                chunk = self.frame(number)[within:within + end - offset]
                offset += len(chunk)
            out += chunk
        return bytes(out).ljust(length, b"\x00")
