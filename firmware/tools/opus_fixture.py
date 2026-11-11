import argparse
import math
import os
import struct
import subprocess
from pathlib import Path

from vorbis_fixture import offset_granules, repair_page

RATE = 48000
TRACKS = (
    ("stereo48", 2, 2.0, ["-b:a", "128k", "-frame_duration", "20", "-page_duration", "200000"]),
    ("speech16", 1, 1.0, ["-b:a", "16k", "-application", "voip", "-frame_duration", "20"]),
    ("long120", 2, 1.0, ["-b:a", "96k", "-frame_duration", "120"]),
)
GAIN_Q8 = 768
ORIGIN = 4800


def signal(channels, seconds):
    values = [int(12000 * math.sin(2 * math.pi * ((997 if channel == 0 else 2111) * index / RATE +
                                                   (43 if channel == 0 else 73) * (index / RATE) ** 2 / 2)))
              for index in range(int(RATE * seconds)) for channel in range(channels)]
    return struct.pack(f"<{len(values)}h", *values)


def ffmpeg(arguments, data, env):
    return subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", *arguments],
                          input=data, capture_output=True, check=True, env=env).stdout


def encode(channels, seconds, options, env):
    return ffmpeg(["-f", "s16le", "-ar", str(RATE), "-ac", str(channels), "-i", "-",
                   "-c:a", "libopus", *options, "-fflags", "+bitexact", "-flags:a", "+bitexact",
                   "-map_metadata", "-1", "-f", "ogg", "-"], signal(channels, seconds), env)


def decode(encoded, env):
    return ffmpeg(["-c:a", "libopus", "-f", "ogg", "-i", "-", "-f", "s16le", "-"], encoded, env)


def with_gain(encoded, gain):
    data = bytearray(encoded)
    if data[28:36] != b"OpusHead":
        raise ValueError("the first page must hold the identification header alone")
    struct.pack_into("<h", data, 28 + 16, gain)
    repair_page(data, 0, 27 + data[26] + sum(data[27:27 + data[26]]))
    return bytes(data)


def peak(pcm):
    return max(abs(value) for value in struct.unpack(f"<{len(pcm) // 2}h", pcm))


def main():
    parser = argparse.ArgumentParser(description="Generate Ogg Opus fixtures with an independent libopus reference")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "tests/fixtures/opus")
    parser.add_argument("--build", type=Path, default=Path("/Volumes/Scratch/SnowskyEcho/tmp/opus-fixtures"))
    args = parser.parse_args()
    scratch = Path("/Volumes/Scratch")
    if not scratch.is_mount() or not args.build.resolve().is_relative_to(scratch):
        parser.error("the build directory must be on the mounted Scratch volume")
    args.build.mkdir(parents=True, exist_ok=True)
    env = os.environ | {"TMPDIR": str(args.build)}
    args.output.mkdir(parents=True, exist_ok=True)
    encoded = {}
    for name, channels, seconds, options in TRACKS:
        encoded[name] = encode(channels, seconds, options, env)
        pcm = decode(encoded[name], env)
        if len(pcm) != int(RATE * seconds) * channels * 2:
            raise ValueError(f"{name}: the reference length differs from the source length")
        (args.output / f"{name}.opus").write_bytes(encoded[name])
        (args.output / f"{name}.pcm").write_bytes(pcm)
    louder = with_gain(encoded["long120"], GAIN_Q8)
    pcm = decode(louder, env)
    plain = (args.output / "long120.pcm").read_bytes()
    if not 1.3 < peak(pcm) / peak(plain) < 1.5:
        raise ValueError("the reference decoder did not apply the header gain")
    (args.output / "gain120.opus").write_bytes(louder)
    (args.output / "gain120.pcm").write_bytes(pcm)
    (args.output / "origin48.opus").write_bytes(offset_granules(encoded["stereo48"], ORIGIN))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
