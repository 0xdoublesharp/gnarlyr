import argparse
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from echo_emulator import core1

ARGUMENTS = {}
FIXTURES = ROOT / "tests" / "fixtures"
ALBUMS = {"tail": "Aac Artist/Aac Album/02 Tail.m4a", "lead": "Aac Artist/Aac Album/01 Lead.m4a", "mono": "Aac Artist/Aac Album/03 Mono.m4a",
          "lossless": "Alac Artist/Alac Album/01 Lossless.m4a",
          "deep": "Alac Artist/Alac Album/02 Deep.m4a"}
AAC_TOLERANCE = 3 << 8


def reference(name, width, channels):
    data = (FIXTURES / "m4a_pcm" / f"{name}.pcm").read_bytes()
    kind = "h" if width == 2 else "i"
    values = struct.unpack(f"<{len(data) // width}{kind}", data)
    if width == 4:
        values = tuple(value >> 8 for value in values)
    if name == "tail":
        values = values[1024 * channels:(1024 + 44100) * channels]
    return [values[index::channels] for index in range(channels)]


class Core1M4aTest(unittest.TestCase):
    def decode(self, name):
        path = FIXTURES / "m4a_music" / ALBUMS[name]
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch) / "stream.bin"
            run = subprocess.run([ARGUMENTS["tool"], f"m4a_music/{ALBUMS[name]}", "0", str(output)],
                                 capture_output=True, text=True, check=True)
            codec, channels, rate, frame, delay, valid = (int(item) for item in run.stdout.split())
            stream = output.read_bytes()
        program = Path(ARGUMENTS["program"]).read_bytes()
        bits = 24 if codec == 1 else 16
        blocks = core1.decode_stream(program, stream, channels, bits, 0)
        self.assertTrue(path.exists())
        return codec, channels, frame, delay, valid, blocks

    def check_blocks(self, name, width):
        codec, channels, frame, delay, valid, blocks = self.decode(name)
        wanted = reference(name, width, channels)
        self.assertEqual(core1.RESULT_END, blocks[-1][0])
        total = 0
        for result, first, count, data, _ in blocks[:-1]:
            self.assertEqual(core1.RESULT_READY, result)
            for channel in range(channels):
                for index in range(count):
                    absolute = first + index - delay
                    if absolute < 0 or absolute >= valid:
                        continue
                    got, expected = data[channel][index], wanted[channel][absolute]
                    limit = AAC_TOLERANCE if codec == 1 else 0
                    scale = 256 if codec == 1 else 1
                    self.assertLessEqual(abs(got - expected * scale), limit)
            total += count
        self.assertGreaterEqual(total, valid)
        return blocks, frame

    def test_the_aac_program_decodes_a_stereo_file_like_the_reference(self):
        self.check_blocks("lead", 2)

    def test_the_aac_program_matches_the_edit_trimmed_reference(self):
        self.check_blocks("tail", 2)

    def test_the_aac_program_decodes_a_mono_file_like_the_reference(self):
        self.check_blocks("mono", 2)

    def test_the_alac_program_decodes_16_bit_stereo_exactly(self):
        self.check_blocks("lossless", 2)

    def test_the_alac_program_decodes_24_bit_stereo_exactly(self):
        self.check_blocks("deep", 4)

    def test_aac_decode_cost_per_frame_is_reported(self):
        blocks, _ = self.check_blocks("lead", 2)
        times = [block[4] for block in blocks[:-1]]
        per_frame = (times[-1] - times[0]) / (len(times) - 1)
        print(f"AAC: {per_frame:,.0f} instructions per 1024-sample frame")
        self.assertLess(per_frame, 2_300_000)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", required=True)
    parser.add_argument("--tool", required=True)
    known, rest = parser.parse_known_args()
    if not Path(known.program).exists() or not Path(known.tool).exists():
        sys.exit(77)
    ARGUMENTS.update(program=known.program, tool=known.tool)
    unittest.main(argv=[sys.argv[0], *rest])
