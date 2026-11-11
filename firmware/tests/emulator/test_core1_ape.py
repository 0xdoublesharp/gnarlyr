import argparse
import struct
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from echo_emulator.core1 import decode_stream, RESULT_READY, RESULT_END


def check(program, tool, fixture, frame):
    encoded = subprocess.check_output([str(tool), str(fixture), str(frame)])
    channels, bits, rate, samples, first = struct.unpack_from("<5I", encoded)
    blocks = decode_stream(program, encoded[20:], channels, bits, first)
    position = first
    for result, start, count, pcm, instructions in blocks:
        if result == RESULT_END:
            break
        if result != RESULT_READY or start != position or count > 256:
            raise AssertionError((result, start, position, count))
        for channel in range(channels):
            step = 31 if channel == 0 else 47
            scale = 257 if bits == 24 else 1
            expected = tuple((((index * step) % 30001) - 15000) * scale
                             for index in range(position, position + count))
            if pcm[channel] != expected:
                raise AssertionError((fixture.name, position, channel, "PCM mismatch"))
        position += count
    if blocks[-1][0] != RESULT_END or position != samples:
        raise AssertionError((fixture.name, position, samples, blocks[-1][:3]))
    cost = round(blocks[-1][4] * rate / ((samples - first) * 50))
    print(f"{fixture.name} frame {frame}: {samples - first} exact samples, {cost:,} instructions/20 ms")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    args = parser.parse_args()
    program = args.program.read_bytes()
    for name in ("fast16", "normal24", "high16", "extra16", "insane16", "normal16", "fast24stereo", "normal24stereo", "high24stereo", "seek16"):
        check(program, args.tool, args.fixtures / f"{name}.ape", 0)
    check(program, args.tool, args.fixtures / "seek16.ape", 1)


if __name__ == "__main__":
    main()
