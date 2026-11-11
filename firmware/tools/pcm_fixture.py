import struct


def aiff(pcm, rate=44100, channels=2, bits=16, compression=None):
    exponent = rate.bit_length() - 1
    extended = struct.pack(">HQ", 16383 + exponent, rate << (63 - exponent))
    common = struct.pack(">HIH", channels, len(pcm) // (channels * bits // 8), bits) + extended
    kind = b"AIFF" if compression is None else b"AIFC"
    if compression is not None:
        common += compression + b"\x00\x00"
    sound = bytes(8) + pcm
    body = kind + chunk(b"COMM", common) + chunk(b"SSND", sound)
    return b"FORM" + struct.pack(">I", len(body)) + body


def chunk(kind, body):
    return kind + struct.pack(">I", len(body)) + body + bytes(len(body) & 1)
