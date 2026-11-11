import argparse
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from dsd_fixture import signal, dsf, dff
from echo_emulator.core1 import decode_stream

ARGUMENTS = {}


class DsdProgramTest(unittest.TestCase):
    def test_dsf_and_dff_match_native_pcm_and_report_steady_instruction_cost(self):
        program = Path(ARGUMENTS["program"]).read_bytes()
        report = []
        for ratio in (1, 2, 4):
            for channels in (1, 2):
                data, _ = signal(2822400 * ratio, frames=6000, channels=channels)
                for container, extension in ((dsf, "dsf"), (dff, "dff")):
                    with tempfile.TemporaryDirectory() as temporary:
                        root = Path(temporary)
                        source = root / f"music.{extension}"
                        transport, pcm = root / "transport.bin", root / "pcm.bin"
                        source.write_bytes(container(data, 2822400 * ratio))
                        subprocess.run([ARGUMENTS["tool"], source, transport, pcm], check=True)
                        blocks = decode_stream(program, transport.read_bytes(), channels, 24, 0,
                                               ring_bytes=32768)
                        actual, ready = [], []
                        for result, first, count, values, instructions in blocks:
                            if result == 0:
                                self.assertEqual(len(actual) // channels, first)
                                actual.extend(value for frame in zip(*values) for value in frame)
                                ready.append((first + count, instructions))
                            else:
                                self.assertEqual(2, result)
                        expected = struct.unpack(f"<{len(actual)}i", pcm.read_bytes())
                        self.assertEqual(expected, tuple(actual))
                        self.assertEqual(6000 * channels, len(actual))
                        frames = ready[-1][0] - ready[1][0]
                        instructions = ready[-1][1] - ready[1][1]
                        cost = round(instructions * 1764 / frames)
                        accepted = ratio == 1 or (ratio == 2 and channels == 1)
                        self.assertEqual(accepted, cost < 2_000_000)
                        peak = max(round((later[1] - before[1]) * 1764 / (later[0] - before[0]))
                                   for before, later in zip(ready[1:-2], ready[2:-1]))
                        line = (f"DSD{64 * ratio} {channels}ch {extension}: {cost:,} mean, "
                                f"{peak:,} peak instructions per 20 ms")
                        report.append(line)
                        print(line, flush=True)
        Path(ARGUMENTS["program"]).with_name("dsd-instructions.txt").write_text("\n".join(report) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", required=True)
    parser.add_argument("--tool", required=True)
    args, remaining = parser.parse_known_args()
    ARGUMENTS.update(vars(args))
    unittest.main(argv=[sys.argv[0], *remaining])
