import argparse
import os
import struct
import subprocess
import wave
from pathlib import Path

TRACKS = (
    ("fast16", 1000, 16, 2, 48000, 6144),
    ("normal24", 2000, 24, 1, 192000, 6144),
    ("high16", 3000, 16, 2, 48000, 6144),
    ("extra16", 4000, 16, 2, 48000, 6144),
    ("insane16", 5000, 16, 2, 48000, 6144),
    ("normal16", 2000, 16, 2, 48000, 6144),
    ("fast24stereo", 1000, 24, 2, 192000, 6144),
    ("normal24stereo", 2000, 24, 2, 192000, 6144),
    ("high24stereo", 3000, 24, 2, 96000, 6144),
    ("fast24stereo96", 1000, 24, 2, 96000, 6144),
    ("normal24stereo96", 2000, 24, 2, 96000, 6144),
    ("high24mono96", 3000, 24, 1, 96000, 6144),
    ("seek16", 2000, 16, 1, 8000, 295936),
)


def sample(index, channel, bits):
    value = ((index * (31 if channel == 0 else 47)) % 30001) - 15000
    return value if bits == 16 else value * 257


def generate(encoder, output, scratch):
    output.mkdir(parents=True, exist_ok=True)
    scratch.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TMPDIR=str(scratch))
    for name, level, bits, channels, rate, frames in TRACKS:
        wav = scratch / f"{name}.wav"
        pcm = b"".join(sample(index, channel, bits).to_bytes(bits // 8, "little", signed=True)
                       for index in range(frames) for channel in range(channels))
        with wave.open(str(wav), "wb") as file:
            file.setparams((channels, bits // 8, rate, frames, "NONE", "not compressed"))
            file.writeframes(pcm)
        subprocess.run([str(encoder), str(wav), str(output / f"{name}.ape"), f"-c{level}"],
                       env=env, check=True, capture_output=True)
        decoded = subprocess.run(["ffmpeg", "-v", "error", "-i", str(output / f"{name}.ape"),
                                  "-f", f"s{bits}le", "-"], env=env, check=True, capture_output=True).stdout
        if decoded != pcm:
            raise ValueError(f"{name} failed the independent ffmpeg reference decode")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--encoder", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scratch", type=Path, required=True)
    args = parser.parse_args()
    generate(args.encoder.resolve(), args.output.resolve(), args.scratch.resolve())


if __name__ == "__main__":
    main()
