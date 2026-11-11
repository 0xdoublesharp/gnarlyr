import argparse
import array
import math
import struct
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from echo_emulator.core1 import decode_stream, RESULT_READY, RESULT_END

TRACKS = (("v1mono", 16000, 1, 12), ("v2stereo", 48000, 2, 2), ("v2seek", 44100, 2, 2),
          ("v2fragment", 44100, 2, 2), ("v1compressed", 16000, 1, 12))


def reference(path):
    return array.array("h", subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(path),
                                                    "-f", "s16le", "-"]))


def check_pcm(values, expected, tolerance, label):
    if len(values) != len(expected):
        raise AssertionError((label, "length", len(values), len(expected)))
    maximum = max(abs(value / 8192 - sample) for value, sample in zip(values, expected))
    if maximum > tolerance:
        raise AssertionError((label, "PCM difference", maximum, tolerance))
    print(f"{label}: {len(values)} samples, max {maximum:.4f} LSB16")


def records(tool, fixture, milliseconds):
    data = subprocess.check_output([str(tool), str(fixture), str(milliseconds), "records"])
    info = struct.unpack_from("<6I", data)
    return info, data[24:]


def native(tool, fixtures):
    for name, rate, channels, tolerance in TRACKS:
        fixture = fixtures / f"{name}.wma"
        expected = reference(fixture)
        for milliseconds in ((0, 700, 1300, 1900) if name == "v2seek" else (0,)):
            decoded = subprocess.check_output([str(tool), str(fixture), str(milliseconds)])
            values = array.array("i", decoded)
            first = milliseconds * rate // 1000 * channels
            check_pcm(values, expected[first:], tolerance, f"native {name} at {milliseconds} ms")
        info, stream = records(tool, fixture, 0)
        cursor = 32
        payloads = bytearray()
        while True:
            size = struct.unpack_from("<I", stream, cursor)[0]
            cursor += 4
            if not size:
                break
            payloads += stream[cursor:cursor + size]
            cursor += size
        independent = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(fixture),
                                              "-map", "0:a:0", "-c", "copy", "-f", "data", "-"])
        if payloads != independent:
            raise AssertionError((name, "ASF payloads differ from ffmpeg"))
        if info[3] != len(expected) // channels:
            raise AssertionError((name, "metadata sample count", info[3], len(expected) // channels))
    actual = array.array("i", subprocess.check_output([str(tool), "--imdct"]))
    expected = [1048576 * math.cos(math.pi / 128 * (index + 0.5 + 64) * 0.5)
                for index in range(256)]
    if max(abs(value - sample) for value, sample in zip(actual, expected)) > 8:
        raise AssertionError("the fixed-point transform failed the independent cosine basis")


def arm(program, tool, fixtures):
    for name, rate, channels, tolerance in TRACKS:
        fixture = fixtures / f"{name}.wma"
        expected = reference(fixture)
        for milliseconds in ((0, 1300) if name == "v2seek" else (0,)):
            info, stream = records(tool, fixture, milliseconds)
            _, bits, _, samples, first, target = info
            blocks = decode_stream(program, stream, channels, bits, first, ring_bytes=8192)
            values = array.array("i")
            position = first
            for result, start, count, pcm, instructions in blocks:
                if result == RESULT_END:
                    break
                if result != RESULT_READY or start != position or count > 2048:
                    raise AssertionError((name, result, start, position, count))
                skip = max(0, target - position)
                for frame in range(min(skip, count), count):
                    values.extend(pcm[channel][frame] for channel in range(channels))
                position += count
            if blocks[-1][0] != RESULT_END or position != samples:
                raise AssertionError((name, "stream end", position, samples, blocks[-1][:3]))
            check_pcm(values, expected[target * channels:], tolerance, f"ARM {name} at {milliseconds} ms")
            cost = round(blocks[-1][4] * rate / ((samples - first) * 50))
            print(f"{name}: {cost:,} instructions/20 ms")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", type=Path)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    args = parser.parse_args()
    if args.program:
        arm(args.program.read_bytes(), args.tool, args.fixtures)
    else:
        native(args.tool, args.fixtures)


if __name__ == "__main__":
    main()
