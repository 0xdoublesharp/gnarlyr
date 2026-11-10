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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
