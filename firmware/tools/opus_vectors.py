"""Decode the RFC 8251 Opus test vectors with the core 1 Opus program in the emulator.

Each `testvectorNN.bit` (opus_demo's format: big-endian length and final range, then the packet)
becomes core 1's record stream: an identification header with no pre-skip, every packet with its
end granule, and the end record. The 16-bit output is checked with libopus's `opus_compare`
against `testvectorNN.dec` in stereo and `testvectorNNm.dec` in mono. Stereo output is also
counted sample by sample against the reference; the mono references hold two channels, which
`opus_compare` averages, so they get no sample count. Download the vectors from opus-codec.org
and build `opus_compare` from `third_party/opus/src/opus_compare.c` on Scratch.
"""
import argparse
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

import core1_bench

FRAME_SIZES = [480, 960, 1920, 2880] * 3 + [480, 960] * 2 + [120, 240, 480, 960] * 4
FIRST = 1
END = 4


def packet_frames(packet):
    size = FRAME_SIZES[packet[0] >> 3]
    code = packet[0] & 3
    count = 1 if code == 0 else 2 if code in (1, 2) else packet[1] & 0x3F
    return size * count


def records(bitstream, channels):
    head = b"OpusHead" + struct.pack("<BBHIhB", 1, channels, 0, 48000, 0, 0)
    out = [struct.pack("<IIQ", len(head), FIRST, (1 << 64) - 1) + head]
    offset = 0
    granule = 0
    while offset < len(bitstream):
        length, _ = struct.unpack_from(">II", bitstream, offset)
        packet = bitstream[offset + 8:offset + 8 + length]
        offset += 8 + length
        granule += packet_frames(packet)
        out.append(struct.pack("<IIQ", length, 0, granule) + packet)
    out.append(struct.pack("<IIQ", 0, END, (1 << 64) - 1))
    return b"".join(out)


def decode(program, bitstream, channels):
    blocks = core1_bench.stream_decode(program, records(bitstream, channels), channels, 24,
                                       ring=40960)
    if blocks[-1][0] != 2:
        raise RuntimeError("the vector did not decode to its end")
    samples = []
    for _, _, count, data, _ in blocks[:-1]:
        for index in range(count):
            samples.extend(data[channel][index] >> 8 for channel in range(channels))
    return samples


def check(job):
    program, vector, channels, compare = job
    reference_path = vector.with_name(vector.stem + ("" if channels == 2 else "m") + ".dec")
    samples = decode(Path(program).read_bytes(), vector.read_bytes(), channels)
    reference = reference_path.read_bytes()
    wanted = struct.unpack(f"<{len(reference) // 2}h", reference) if channels == 2 else ()
    different = sum(1 for got, expected in zip(samples, wanted) if got != expected)
    largest = max((abs(got - expected) for got, expected in zip(samples, wanted)), default=0)
    with tempfile.TemporaryDirectory() as scratch:
        output = Path(scratch) / "out.sw"
        output.write_bytes(struct.pack(f"<{len(samples)}h", *samples))
        command = [compare, *(["-s"] if channels == 2 else []), str(reference_path), str(output)]
        run = subprocess.run(command, capture_output=True, text=True)
    verdict = (run.stdout + run.stderr).strip().splitlines()
    return (f"{vector.stem} {'stereo' if channels == 2 else 'mono'}: "
            f"{'PASS' if run.returncode == 0 else 'FAIL'} ({verdict[-1] if verdict else ''}); "
            + (f"{different} of {len(wanted)} samples differ, largest {largest}; " if wanted else "")
            + f"{len(samples)} decoded"), run.returncode == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--program", default=str(core1_bench.DEFAULT_DEVICE / "core1_opus.bin"))
    parser.add_argument("--vectors", required=True)
    parser.add_argument("--compare", required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--only")
    arguments = parser.parse_args()
    vectors = sorted(Path(arguments.vectors).glob("testvector*.bit"))
    if arguments.only:
        vectors = [vector for vector in vectors if arguments.only in vector.stem]
    jobs = [(arguments.program, vector, channels, arguments.compare)
            for vector in vectors for channels in (2, 1)]
    passed = True
    with ProcessPoolExecutor(arguments.jobs) as pool:
        for line, ok in pool.map(check, jobs):
            print(line, flush=True)
            passed = passed and ok
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
