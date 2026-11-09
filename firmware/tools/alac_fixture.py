import argparse
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

FFMPEG = "ffmpeg"
COOKIE_BYTES = 24
FIXTURES = [
    {"name": "music_16_44k_stereo", "rate": 44100, "bits": 16, "channels": 2, "frames": 9000,
     "flags": [], "signal": "music"},
    {"name": "music_16_mono", "rate": 44100, "bits": 16, "channels": 1, "frames": 9000,
     "flags": [], "signal": "music"},
    {"name": "music_24_48k_stereo", "rate": 48000, "bits": 24, "channels": 2, "frames": 9000,
     "flags": [], "signal": "music"},
    {"name": "music_24_mono", "rate": 48000, "bits": 24, "channels": 1, "frames": 9000,
     "flags": [], "signal": "music"},
    {"name": "verbatim_16_stereo", "rate": 44100, "bits": 16, "channels": 2, "frames": 3000,
     "flags": ["-compression_level", "0"], "signal": "music"},
    {"name": "verbatim_24_stereo", "rate": 48000, "bits": 24, "channels": 2, "frames": 3000,
     "flags": ["-compression_level", "0"], "signal": "music"},
    {"name": "low_order_16_stereo", "rate": 44100, "bits": 16, "channels": 2, "frames": 9000,
     "flags": ["-min_prediction_order", "1", "-max_prediction_order", "2"], "signal": "music"},
    {"name": "high_order_16_stereo", "rate": 44100, "bits": 16, "channels": 2, "frames": 9000,
     "flags": ["-min_prediction_order", "24", "-max_prediction_order", "30"], "signal": "music"},
    {"name": "gaps_16_stereo", "rate": 44100, "bits": 16, "channels": 2, "frames": 9000,
     "flags": [], "signal": "gaps"},
    {"name": "gaps_24_stereo", "rate": 48000, "bits": 24, "channels": 2, "frames": 9000,
     "flags": [], "signal": "gaps"},
]


class Noise:
    def __init__(self, seed):
        self.state = seed

    def next(self):
        self.state = (self.state * 1103515245 + 12345) & 0x7FFFFFFF
        return self.state / 0x7FFFFFFF * 2 - 1


def music_sample(index, channel, rate, noise):
    time = index / rate
    tone = math.sin(2 * math.pi * (440 + 220 * channel) * time)
    overtone = 0.3 * math.sin(2 * math.pi * 1250 * time + channel)
    envelope = 0.5 + 0.5 * math.sin(2 * math.pi * 0.7 * time)
    return (0.5 * tone + 0.25 * overtone) * envelope + 0.04 * noise.next()


def gap_sample(index, channel, noise):
    segment = index // 1500
    if segment % 4 == 0:
        return 0.0
    if segment % 4 == 1:
        return noise.next() * 0.9
    if segment % 4 == 2:
        return -0.0375 if channel == 0 else 0.1317
    return 0.4 * math.sin(index / 9.0 + channel)


def samples_for(fixture):
    peak = (1 << (fixture["bits"] - 1)) - 1
    noise = Noise(7 + fixture["channels"] + fixture["bits"])
    rows = []
    for index in range(fixture["frames"]):
        row = []
        for channel in range(fixture["channels"]):
            if fixture["signal"] == "gaps":
                value = gap_sample(index, channel, noise)
            else:
                value = music_sample(index, channel, fixture["rate"], noise)
            row.append(max(-peak - 1, min(peak, int(value * peak))))
        rows.append(row)
    return rows


def encoder_input(rows, bits):
    shift = 32 - bits
    return b"".join(struct.pack("<i", value << shift) for row in rows for value in row)


def reference_pcm(rows, bits):
    code = "<h" if bits == 16 else "<i"
    return b"".join(struct.pack(code, value) for row in rows for value in row)


def encode(fixture, raw, output):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "input.raw"
        source.write_bytes(raw)
        sample_format = "s16p" if fixture["bits"] == 16 else "s32p"
        command = [str(FFMPEG), "-v", "error", "-y", "-f", "s32le", "-ar", str(fixture["rate"]),
                   "-ac", str(fixture["channels"]), "-i", str(source)]
        if fixture["bits"] == 16:
            command += ["-af", "aformat=sample_fmts=s16"]
        command += ["-c:a", "alac", "-sample_fmt", sample_format, "-map_metadata", "-1",
                    "-fflags", "+bitexact", "-flags", "+bitexact", *fixture["flags"], str(output)]
        subprocess.run(command, check=True)


def boxes(data, start, end):
    position = start
    while position + 8 <= end:
        size, kind = struct.unpack_from(">I4s", data, position)
        header = 8
        if size == 1:
            size = struct.unpack_from(">Q", data, position + 8)[0]
            header = 16
        if size == 0:
            size = end - position
        yield kind, position + header, position + size
        position += size


def find_box(data, start, end, path):
    for kind, inner_start, inner_end in boxes(data, start, end):
        if kind.decode("latin1") == path[0]:
            if len(path) == 1:
                return inner_start, inner_end
            return find_box(data, inner_start, inner_end, path[1:])
    raise ValueError(f"box {path[0]} not found")


def find_alac_cookie(data, start, end):
    entry = start + 8
    child = entry + 8 + 28
    for kind, inner_start, inner_end in boxes(data, child, end):
        if kind == b"alac":
            return data[inner_start + 4:inner_start + 4 + COOKIE_BYTES]
    raise ValueError("alac cookie not found")


def read_sample_sizes(data, start):
    fixed, count = struct.unpack_from(">II", data, start + 4)
    if fixed != 0:
        return [fixed] * count
    return list(struct.unpack_from(f">{count}I", data, start + 12))


def read_chunk_table(data, start):
    count = struct.unpack_from(">I", data, start + 4)[0]
    return [struct.unpack_from(">III", data, start + 8 + 12 * entry) for entry in range(count)]


def read_chunk_offsets(data, start):
    count = struct.unpack_from(">I", data, start + 4)[0]
    return list(struct.unpack_from(f">{count}I", data, start + 8))


def samples_per_chunk(table, chunk_count):
    result = []
    for chunk in range(1, chunk_count + 1):
        current = [entry for entry in table if entry[0] <= chunk][-1]
        result.append(current[1])
    return result


def extract_packets(data):
    moov = find_box(data, 0, len(data), ["moov"])
    stbl = find_box(data, *moov, ["trak", "mdia", "minf", "stbl"])
    stsd = find_box(data, *stbl, ["stsd"])
    cookie = find_alac_cookie(data, *stsd)
    sizes = read_sample_sizes(data, find_box(data, *stbl, ["stsz"])[0])
    table = read_chunk_table(data, find_box(data, *stbl, ["stsc"])[0])
    offsets = read_chunk_offsets(data, find_box(data, *stbl, ["stco"])[0])
    packets = []
    cursor = 0
    for offset, per_chunk in zip(offsets, samples_per_chunk(table, len(offsets))):
        position = offset
        for _ in range(per_chunk):
            packets.append(data[position:position + sizes[cursor]])
            position += sizes[cursor]
            cursor += 1
    return cookie, packets


def pack_packets(cookie, packets):
    out = bytearray(cookie)
    out += struct.pack("<I", len(packets))
    for packet in packets:
        out += struct.pack("<I", len(packet)) + packet
    return bytes(out)


def build(fixture, directory):
    rows = samples_for(fixture)
    container = directory / f"{fixture['name']}.m4a"
    encode(fixture, encoder_input(rows, fixture["bits"]), container)
    cookie, packets = extract_packets(container.read_bytes())
    return {
        f"{fixture['name']}.alac": pack_packets(cookie, packets),
        f"{fixture['name']}.pcm": reference_pcm(rows, fixture["bits"]),
    }


def build_all():
    files = {}
    with tempfile.TemporaryDirectory() as directory:
        for fixture in FIXTURES:
            files.update(build(fixture, Path(directory)))
    return files


def check(files, output):
    stale = [name for name, content in files.items()
             if not (output / name).is_file() or (output / name).read_bytes() != content]
    for name in stale:
        print(f"stale: {name}")
    return 1 if stale else 0


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Write the ALAC packets and expected samples used by the C tests")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    files = build_all()
    if arguments.check:
        return check(files, arguments.output)
    arguments.output.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        (arguments.output / name).write_bytes(content)
        print(f"wrote {name} ({len(content)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
