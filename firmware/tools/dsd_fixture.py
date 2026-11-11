import math
import struct


def signal(rate, frames=6000, kind="sine", channels=2):
    samples = frames * (rate // 88200)
    data = [bytearray((samples + 7) // 8) for _ in range(channels)]
    for channel in range(channels):
        error = 0.0
        for index in range(samples):
            time = index / rate
            phase = 2 * math.pi * ((1000 + channel * 321) * time if kind == "sine"
                                    else 500 * time + 100000 * time * time)
            wanted = 0.0 if kind == "silence" else 0.35 * math.sin(phase)
            error += wanted
            bit = int(error >= 0)
            error -= 2 * bit - 1
            data[channel][index // 8] |= bit << (index % 8)
    return [bytes(value) for value in data], samples


def dsf(data, rate, samples=None, msb=False):
    channels = len(data)
    samples = len(data[0]) * 8 if samples is None else samples
    blocks = (len(data[0]) + 4095) // 4096
    payload = b"".join(channel[start:start + 4096].ljust(4096, b"\x00")
                       for start in range(0, blocks * 4096, 4096) for channel in data)
    if msb:
        payload = reverse(payload)
    size = 92 + len(payload)
    return (struct.pack("<4sQQQ", b"DSD ", 28, size, 0)
            + struct.pack("<4sQIIIIIIQII", b"fmt ", 52, 1, 0, channels, channels, rate,
                          8 if msb else 1, samples, 4096, 0)
            + struct.pack("<4sQ", b"data", 12 + len(payload)) + payload)


def reverse(data):
    return bytes(int(f"{byte:08b}"[::-1], 2) for byte in data)


def chunk(tag, body):
    return tag + struct.pack(">Q", len(body)) + body + bytes(len(body) & 1)


def dff(data, rate, artist=None, title=None):
    channels = len(data)
    names = (b"SLFT" + b"SRGT")[:4 * channels]
    properties = (b"SND " + chunk(b"FS  ", struct.pack(">I", rate))
                  + chunk(b"CHNL", struct.pack(">H", channels) + names)
                  + chunk(b"CMPR", b"DSD \x00"))
    payload = reverse(bytes(value for frame in zip(*data) for value in frame))
    body = b"DSD " + chunk(b"FVER", struct.pack(">I", 0x01050000))
    body += chunk(b"PROP", properties) + chunk(b"DSD ", payload)
    entries = b""
    for tag, value in ((b"DIAR", artist), (b"DITI", title)):
        if value is not None:
            encoded = value.encode("ascii")
            entries += chunk(tag, struct.pack(">I", len(encoded)) + encoded)
    if entries:
        body += chunk(b"DIIN", entries)
    return b"FRM8" + struct.pack(">Q", len(body)) + body
