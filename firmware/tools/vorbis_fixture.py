import argparse
import math
import os
import struct
import subprocess
from pathlib import Path

TRACKS = (("mono8", 1, 8000, "0.4"), ("stereo44", 2, 44100, "0.4"),
          ("stereo48", 2, 48000, "0.9"), ("low48", 2, 48000, "-0.1"), ("seek44", 2, 44100, "0.4"),
          ("seek8", 1, 8000, "0.4"))


def signal(channels, rate, long=False):
    if long:
        values = [int(12000 * math.sin(2 * math.pi * ((997 if channel == 0 else 2111) * index / rate +
                                                   (43 if channel == 0 else 73) * (index / rate) ** 2 / 2)))
                  for index in range(rate * 13 // 4) for channel in range(channels)]
    else:
        values = [int(12000 * math.sin(2 * math.pi * (997 if channel == 0 else 2111) * index / rate))
                  for index in range(rate // 4) for channel in range(channels)]
    return struct.pack(f"<{len(values)}h", *values)


def repair_page(data, offset, size):
    data[offset + 22:offset + 26] = bytes(4)
    crc = 0
    for byte in data[offset:offset + size]:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04C11DB7 if crc & 0x80000000 else 0)) & 0xffffffff
    struct.pack_into("<I", data, offset + 22, crc)


def offset_granules(encoded, delta):
    data = bytearray(encoded)
    offset = 0
    while offset < len(data):
        count = data[offset + 26]
        size = 27 + count + sum(data[offset + 27:offset + 27 + count])
        granule = struct.unpack_from("<Q", data, offset + 6)[0]
        if 0 < granule < (1 << 64) - 1:
            struct.pack_into("<Q", data, offset + 6, granule + delta)
            repair_page(data, offset, size)
        offset += size
    return bytes(data)


def other_page(sequence, flags):
    data = bytearray(b"OggS" + bytes([0, flags]) + struct.pack("<QIII", 0, 77, sequence, 0) + bytes([1, 5]) + b"other")
    repair_page(data, 0, len(data))
    return bytes(data)


def logical_fixtures(output):
    first = (output / "stereo44.ogg").read_bytes()
    second = bytearray((output / "mono8.ogg").read_bytes())
    offset = 0
    while offset < len(second):
        count = second[offset + 26]
        size = 27 + count + sum(second[offset + 27:offset + 27 + count])
        struct.pack_into("<I", second, offset + 14, 43)
        repair_page(second, offset, size)
        offset += size
    (output / "chained44.ogg").write_bytes(first + second)
    audio = (output / "seek44.ogg").read_bytes()
    multiplexed = bytearray()
    offset = 0
    sequence = 0
    while offset < len(audio):
        count = audio[offset + 26]
        size = 27 + count + sum(audio[offset + 27:offset + 27 + count])
        multiplexed += audio[offset:offset + size]
        multiplexed += other_page(sequence, 2 if sequence == 0 else (4 if offset + size == len(audio) else 0))
        sequence += 1
        offset += size
    (output / "multiplexed44.ogg").write_bytes(multiplexed)


def main():
    parser = argparse.ArgumentParser(description="Generate independent Xiph Vorbis decoder fixtures")
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "tests/fixtures/vorbis")
    parser.add_argument("--build", type=Path, default=Path("/Volumes/Scratch/SnowskyEcho/tmp/vorbis-fixtures"))
    args = parser.parse_args()
    scratch = Path("/Volumes/Scratch")
    if not scratch.is_mount() or not args.build.resolve().is_relative_to(scratch):
        parser.error("the build directory must be on the mounted Scratch volume")
    args.build.mkdir(parents=True, exist_ok=True)
    codec = args.build / "vorbis_fixture_codec"
    prefix = args.prefix.resolve()
    env = os.environ | {"TMPDIR": str(args.build)}
    subprocess.run([env.get("CC", "cc"), "-std=c11", "-O2", str(Path(__file__).with_name("vorbis_fixture_codec.c")),
                    "-I" + str(prefix / "include"), "-L" + str(prefix / "lib"),
                    "-Wl,-rpath," + str(prefix / "lib"), "-lvorbisenc", "-lvorbisfile", "-lvorbis", "-logg", "-lm", "-o", str(codec)],
                   env=env, check=True)
    args.output.mkdir(parents=True, exist_ok=True)
    for name, channels, rate, quality in TRACKS:
        encoded = subprocess.run([str(codec), "encode", str(channels), str(rate), quality],
                                 input=signal(channels, rate, name.startswith("seek")), capture_output=True, check=True, env=env).stdout
        decoded = subprocess.run([str(codec), "decode"], input=encoded, capture_output=True,
                                 check=True, env=env).stdout
        (args.output / f"{name}.ogg").write_bytes(encoded)
        (args.output / f"{name}.pcm").write_bytes(decoded)
    original = (args.output / "seek44.ogg").read_bytes()
    cropped = offset_granules(original, -500)
    decoded = subprocess.run([str(codec), "decode"], input=cropped, capture_output=True,
                             check=True, env=env).stdout
    (args.output / "cropped44.ogg").write_bytes(cropped)
    (args.output / "cropped44.pcm").write_bytes(decoded)
    shifted = subprocess.run([str(codec), "decode"], input=offset_granules(original, 500),
                             capture_output=True, check=True, env=env).stdout
    if shifted != (args.output / "seek44.pcm").read_bytes():
        raise ValueError("the shifted-origin reference differs from the original PCM")
    logical_fixtures(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
