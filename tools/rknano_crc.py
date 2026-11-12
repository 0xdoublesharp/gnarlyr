import re

POLYNOMIAL = 0x04C10DB7


def crc_table():
    table = []
    for index in range(256):
        value = index << 24
        for _ in range(8):
            value = (value << 1) ^ POLYNOMIAL if value & 0x80000000 else value << 1
            value &= 0xFFFFFFFF
        table.append(value)
    return table


CRC_TABLE = crc_table()
ZERO_RUN = re.compile(rb"\x00{256,}")


def crc_bytes(value, data):
    for byte in data:
        value = ((value << 8) & 0xFFFFFFFF) ^ CRC_TABLE[(value >> 24) ^ byte]
    return value


def apply_matrix(matrix, value):
    result = 0
    for column in matrix:
        if value & 1:
            result ^= column
        value >>= 1
    return result


def zero_powers(count):
    matrix = [crc_bytes(1 << bit, b"\x00") for bit in range(32)]
    powers = []
    for _ in range(count):
        powers.append(matrix)
        matrix = [apply_matrix(matrix, column) for column in matrix]
    return powers


ZERO_POWERS = zero_powers(32)


def crc_zeros(value, count):
    for matrix in ZERO_POWERS:
        if not count:
            break
        if count & 1:
            value = apply_matrix(matrix, value)
        count >>= 1
    return value


def crc32(data):
    data = memoryview(data).cast("B")
    value, position = 0, 0
    for run in ZERO_RUN.finditer(data):
        value = crc_bytes(value, data[position:run.start()])
        value = crc_zeros(value, run.end() - run.start())
        position = run.end()
    return crc_bytes(value, data[position:])
