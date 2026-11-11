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
from echo_emulator.machine import load_symbols
from unicorn import UC_HOOK_BLOCK

ARGUMENTS = {}
FIXTURES = ROOT / "tests" / "fixtures" / "opus"
RING_BYTES = 40960
UNKNOWN = (1 << 64) - 1
END = struct.pack("<IIQ", 0, 4, UNKNOWN)
GUARD = 0x0102A000
MAX_ERROR = 16
RMS_ERROR = 4.5
PRE_ROLL = 3840


def records(name):
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / "packets.bin"
        subprocess.run([ARGUMENTS["tool"], str(FIXTURES / f"{name}.opus"), str(output)],
                       capture_output=True, check=True)
        data = output.read_bytes()
    offset = 0
    found = []
    while offset < len(data):
        size, flags, granule = struct.unpack_from("<IIQ", data, offset)
        found.append((flags, granule, data[offset + 16:offset + 16 + size]))
        offset += 16 + size
    if found[1][2][:8] != b"OpusTags":
        raise ValueError("the second packet must be the comment header")
    return [found[0]] + found[2:]


def pack(entries):
    return b"".join(struct.pack("<IIQ", len(body), flags, granule) + body
                    for flags, granule, body in entries)


def reference(name, channels):
    raw = (FIXTURES / f"{name}.pcm").read_bytes()
    return struct.unpack(f"<{len(raw) // 2}h", raw), len(raw) // 2 // channels


def decode(program, stream, channels, start=0, first_sample=0):
    machine = core1.Core1(program, RING_BYTES)
    machine.uc.mem_write(GUARD, b"\xa5" * 0x4000)
    if not machine.boot():
        raise RuntimeError("the Opus program did not report ready")
    machine.start_stream(channels, 24, first_sample)
    machine.set_control("start", start)
    machine.set_control("written", start)
    stream = bytes(start) + stream + END
    fed = start
    blocks = []
    for _ in range(400_000):
        fed = machine.feed(stream, fed)
        machine.run_slice()
        for block in machine.take_slots(channels):
            blocks.append(block)
            if block[0] != core1.RESULT_READY:
                return machine, blocks
    raise RuntimeError("the Opus stream did not finish")


class Core1OpusTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.program = Path(ARGUMENTS["program"]).read_bytes()

    def assert_intact(self, machine):
        self.assertEqual(bytes(machine.uc.mem_read(GUARD, 64)), b"\xa5" * 64)
        self.assertEqual(0, machine.word(0xE000ED28))
        self.assertEqual(0, machine.word(0xE000ED2C))

    def assert_close(self, actual, expected):
        errors = [abs(value - wanted) for value, wanted in zip(actual, expected)]
        self.assertGreater(len(errors), 0)
        self.assertLessEqual(max(errors), MAX_ERROR)
        self.assertLessEqual(sum(error * error for error in errors) / len(errors), RMS_ERROR ** 2)

    def compare(self, blocks, name, channels, offset=0, settle=0):
        expected, length = reference(name, channels)
        count = 0
        actual = []
        wanted = []
        for result, first, frames, data, _ in blocks[:-1]:
            self.assertEqual(core1.RESULT_READY, result)
            self.assertEqual(offset + count, first)
            self.assertLessEqual(frames, core1.BLOCK)
            for frame in range(min(frames, max(length - offset - count, 0))):
                at = offset + count + frame
                for channel in range(channels if count + frame >= settle else 0):
                    self.assertEqual(0, data[channel][frame] % 256)
                    actual.append(data[channel][frame] >> 8)
                    wanted.append(expected[at * channels + channel])
            count += frames
        self.assert_close(actual, wanted)
        return count, length

    def check_reference(self, name, channels, start=0, entries=None):
        stream = pack(records(name) if entries is None else entries)
        machine, blocks = decode(self.program, stream, channels, start)
        self.assertEqual(core1.RESULT_END, blocks[-1][0])
        count, length = self.compare(blocks, name, channels)
        self.assertGreaterEqual(count, length)
        self.assertLess(count - length, 5760)
        self.assert_intact(machine)
        return machine, blocks

    def test_stereo_celt_matches_the_reference_after_pre_skip(self):
        self.check_reference("stereo48", 2)

    def test_mono_speech_matches_the_reference(self):
        entries = records("speech16")
        self.assertEqual(0, entries[1][2][0] & 0x80)
        self.check_reference("speech16", 1)

    def test_120ms_packets_are_published_in_slot_sized_parts(self):
        _, blocks = self.check_reference("long120", 2)
        sizes = {block[2] for block in blocks[:-1]}
        self.assertIn(core1.BLOCK, sizes)
        self.assertIn(5760 - core1.BLOCK, sizes)

    def test_header_output_gain_is_applied(self):
        self.check_reference("gain120", 2)

    def test_records_can_wrap_across_the_input_ring(self):
        self.check_reference("speech16", 1, start=RING_BYTES - 33)

    def test_a_pcm_origin_record_keeps_every_position_and_sample(self):
        entries = records("origin48")
        origin = (8, 4800, b"")
        self.check_reference("stereo48", 2, entries=[entries[0], origin] + entries[1:])

    def test_a_resumed_page_anchors_at_its_first_known_granule(self):
        entries = records("stereo48")
        marked = [index for index, entry in enumerate(entries) if index > 0 and entry[1] != UNKNOWN]
        resume = marked[2] + 1
        anchor = marked[3]
        stream = pack([entries[0]] + entries[resume:])
        machine, blocks = decode(self.program, stream, 2, first_sample=1)
        self.assertEqual(core1.RESULT_END, blocks[-1][0])
        toc = entries[anchor][2][0]
        self.assertEqual((31, 0), (toc >> 3, toc & 3))
        pre_skip = struct.unpack_from("<H", entries[0][2], 10)[0]
        start = entries[anchor][1] - pre_skip - 960
        self.assertEqual(start, blocks[0][1])
        self.compare(blocks, "stereo48", 2, offset=start, settle=PRE_ROLL)
        self.assert_intact(machine)

    def test_malformed_records_leave_through_the_failure_path(self):
        entries = records("speech16")
        head = bytearray(entries[0][2])
        family = bytearray(head)
        family[18] = 1
        stereo = bytearray(head)
        stereo[9] = 2
        origin = (8, 100, b"")
        variants = {
            "audio_first": entries[1:],
            "mapping_family": [(1, 0, bytes(family))] + entries[1:],
            "channel_mismatch": [(1, 0, bytes(stereo))] + entries[1:],
            "head_not_first": [(0, 0, bytes(head))] + entries[1:],
            "short_head": [(1, 0, bytes(head[:18]))] + entries[1:],
            "duplicate_origin": [entries[0], origin, origin] + entries[1:],
            "late_origin": entries[:2] + [origin] + entries[2:],
            "first_audio": [entries[0], (1, UNKNOWN, entries[1][2])] + entries[2:],
            "empty_audio": [entries[0], (0, UNKNOWN, b"")] + entries[1:],
            "early_granule": [entries[0], (8, 1 << 20, b""), (0, 100, entries[1][2])],
        }
        for name, malformed in variants.items():
            with self.subTest(name=name):
                machine, blocks = decode(self.program, pack(malformed), 1)
                self.assertEqual(3, blocks[-1][0])
                self.assert_intact(machine)
        oversized = struct.pack("<IIQ", 32769, 1, 0) + bytes(32769)
        machine, blocks = decode(self.program, oversized, 1)
        self.assertEqual(3, blocks[-1][0])
        self.assertEqual(0, sum(block[2] for block in blocks))

    def test_a_track_change_during_decode_publishes_only_the_new_generation(self):
        machine = core1.Core1(self.program, RING_BYTES)
        self.assertTrue(machine.boot())
        machine.start_stream(2, 24, 0)
        stream = pack(records("stereo48")) + END
        replacement = pack(records("speech16")) + END
        fed = 0
        switched = False
        blocks = []
        symbols = load_symbols(Path(ARGUMENTS["program"]).with_name("echo_core1_opus.elf"))
        address = symbols["opus_decode_packet"]
        calls = []

        def switch(uc, where, size, data):
            nonlocal stream, fed, switched
            calls.append(where)
            if switched:
                return
            switched = True
            stream = replacement
            uc.mem_write(core1.RING, stream + bytes(RING_BYTES - len(stream)))
            fed = len(stream)
            machine.start_stream(1, 24, 0)
            machine.set_control("written", fed)
            machine.set_control("generation", 2)

        machine.uc.hook_add(UC_HOOK_BLOCK, switch, begin=address, end=address)
        for _ in range(400_000):
            fed = machine.feed(stream, fed)
            machine.run_slice()
            blocks.extend(machine.take_slots(machine.control("channels")))
            if blocks and blocks[-1][0] != core1.RESULT_READY:
                break
        self.assertTrue(switched)
        self.assertEqual(core1.RESULT_END, blocks[-1][0])
        expected, length = reference("speech16", 1)
        actual = [value >> 8 for block in blocks[:-1] for value in block[3][0]]
        self.assertGreaterEqual(len(actual), length)
        self.assert_close(actual[:length], expected)
        self.assertEqual(2, machine.control("acked"))

    def test_stereo_decoding_stays_within_the_measured_instruction_budget(self):
        machine, blocks = decode(self.program, pack(records("stereo48")), 2)
        frames = sum(block[2] for block in blocks[:-1])
        per_frame = blocks[-1][4] / (frames / 960)
        print(f"stereo 48 kHz 128 kbit/s: {per_frame:,.0f} instructions per 20 ms", file=sys.stderr)
        self.assertLess(per_frame, 4_000_000)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", required=True)
    parser.add_argument("--tool", required=True)
    arguments, remaining = parser.parse_known_args()
    if not Path(arguments.program).exists() or not Path(arguments.tool).exists():
        sys.exit(77)
    ARGUMENTS.update(program=arguments.program, tool=arguments.tool)
    unittest.main(argv=[sys.argv[0], *remaining])
