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
MAX_INSTRUCTIONS = 80_000_000


class Core1Mp3Test(unittest.TestCase):
    def reference(self, name, start_ms):
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch) / "pcm.bin"
            run = subprocess.run([ARGUMENTS["tool"], f"mp3/{name}.mp3", str(start_ms), str(output)],
                                 capture_output=True, text=True, check=True)
            metadata = tuple(int(value) for value in run.stdout.split())
            pcm = output.read_bytes()
        channels, rate, first, start, end, tail = metadata
        values = struct.unpack(f"<{len(pcm) // 4}i", pcm)
        encoded = (ROOT / "tests" / "fixtures" / "mp3" / f"{name}.mp3").read_bytes()[start:end]
        return channels, first, tail, encoded, values

    def decode(self, channels, first, tail, encoded):
        machine = core1.Core1(Path(ARGUMENTS["program"]).read_bytes(), 65536)
        self.assertTrue(machine.boot())
        machine.start_stream(channels, 24, first)
        machine.set_control("end", len(encoded))
        machine.set_control("tail_trim", tail)
        fed = 0
        blocks = []
        while machine.instructions < MAX_INSTRUCTIONS:
            fed = machine.feed(encoded, fed)
            machine.run_slice()
            for block in machine.take_slots(channels):
                blocks.append(block)
                if block[0] != core1.RESULT_READY:
                    return blocks
        self.fail("the MP3 stream did not reach its end")

    def check_stream(self, name, start_ms, override=None):
        channels, first, tail, encoded, expected = self.reference(name, start_ms)
        tail = tail if override is None else override
        blocks = self.decode(channels, first, tail, encoded)
        self.assertEqual(core1.RESULT_END, blocks[-1][0])
        decoded = []
        next_sample = first
        for result, position, count, samples, _ in blocks[:-1]:
            self.assertEqual(core1.RESULT_READY, result)
            self.assertEqual(next_sample, position)
            self.assertGreater(count, 0)
            for index in range(count):
                decoded.extend(samples[channel][index] for channel in range(channels))
            next_sample += count
        keep = max(0, len(expected) - tail * channels)
        self.assertEqual(tuple(decoded), expected[:keep])

    def test_seeking_stereo_mono_and_mpeg2_keeps_the_exact_trimmed_pcm(self):
        for name, start in [("cbr_128_44k_stereo_tagged", 1000), ("vbr_48k_mono", 1000),
                            ("mpeg2_22k_stereo", 500)]:
            with self.subTest(name=name):
                self.check_stream(name, start)

    def test_starting_at_zero_keeps_the_existing_pcm_and_tail_trim(self):
        self.check_stream("cbr_128_44k_stereo_tagged", 0)

    def test_the_maximum_tail_can_exceed_a_decoder_frame(self):
        self.check_stream("cbr_128_44k_stereo_tagged", 1000, 4095 - 529)

    def test_a_seek_into_the_tail_emits_no_padding(self):
        self.check_stream("cbr_128_44k_stereo_tagged", 1480, 4095 - 529)

    def test_an_untagged_file_keeps_every_decoded_sample(self):
        self.check_stream("cbr_192_48k_stereo_bare", 500)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", required=True)
    parser.add_argument("--tool", required=True)
    known, rest = parser.parse_known_args()
    if not Path(known.program).exists() or not Path(known.tool).exists():
        sys.exit(77)
    ARGUMENTS.update(program=known.program, tool=known.tool)
    unittest.main(argv=[sys.argv[0], *rest])
