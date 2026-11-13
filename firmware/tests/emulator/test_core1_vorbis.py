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
FIXTURES = ROOT / "tests" / "fixtures" / "vorbis"


def packets(name):
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / "packets.bin"
        subprocess.run([ARGUMENTS["tool"], str(FIXTURES / f"{name}.ogg"), str(output)],
                       capture_output=True, check=True)
        return output.read_bytes()


def decode(program, stream, channels, start=0):
    machine = core1.Core1(program, 65536)
    machine.uc.mem_write(0x0102A000, b"\xa5" * 0x4000)
    if not machine.boot():
        raise RuntimeError("the Vorbis program did not report ready")
    machine.start_stream(channels, 24, 0)
    machine.set_control("start", start)
    machine.set_control("written", start)
    stream = bytes(start) + stream + struct.pack("<IIQ", 0, 4, (1 << 64) - 1)
    fed = start
    blocks = []
    for _ in range(200_000):
        fed = machine.feed(stream, fed)
        machine.run_slice()
        for block in machine.take_slots(channels):
            blocks.append(block)
            if block[0] != core1.RESULT_READY:
                return machine, blocks
    raise RuntimeError("the Vorbis stream did not finish")


class Core1VorbisTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.program = Path(ARGUMENTS["program"]).read_bytes()

    def check_reference(self, name, channels, start=0, transport=None):
        stream = packets(name) if transport is None else transport
        machine, blocks = decode(self.program, stream, channels, start)
        self.assertEqual(core1.RESULT_END, blocks[-1][0])
        raw = (FIXTURES / f"{name}.pcm").read_bytes()
        reference = struct.unpack(f"<{len(raw) // 2}h", raw)
        count = 0
        fractional = False
        for result, first, frames, data, _ in blocks[:-1]:
            self.assertEqual(core1.RESULT_READY, result)
            self.assertEqual(count, first)
            for frame in range(frames):
                for channel in range(channels):
                    actual = data[channel][frame]
                    expected = reference[(count + frame) * channels + channel]
                    self.assertLessEqual(abs((actual >> 8) - expected), 2)
                    fractional = fractional or actual % 256 != 0
            count += frames
        self.assertEqual(len(reference) // channels, count)
        self.assertTrue(fractional)
        self.assertEqual(bytes(machine.uc.mem_read(0x0102A000, 64)), b"\xa5" * 64)
        self.assertEqual(1, machine.word(core1.CONTROL + 116))
        self.assertTrue(1 <= machine.word(core1.CONTROL + 104) <= 64)
        identification = (FIXTURES / f"{name}.ogg").read_bytes()
        self.assertEqual(1 << (identification[56] & 15), machine.word(core1.CONTROL + 96))
        self.assertEqual(1 << (identification[56] >> 4), machine.word(core1.CONTROL + 100))
        return machine, blocks

    def test_nonzero_pcm_origin_keeps_every_timestamp_and_reference_sample_contiguous(self):
        stream = bytearray(packets("seek44"))
        offset = 0
        first_audio = None
        index = 0
        while offset < len(stream):
            size, flags, granule = struct.unpack_from("<IIQ", stream, offset)
            if index >= 3:
                if first_audio is None:
                    first_audio = offset
                if granule != (1 << 64) - 1:
                    struct.pack_into("<Q", stream, offset + 8, granule + 500)
            offset += 16 + size
            index += 1
        origin = struct.pack("<IIQ", 0, 8, 500)
        stream = stream[:first_audio] + origin + stream[first_audio:]
        self.check_reference("seek44", 2, transport=bytes(stream))

    def test_invalid_or_repeated_origin_records_leave_through_the_failure_path(self):
        stream = packets("stereo44")
        offset = 0
        boundaries = []
        while offset < len(stream):
            boundaries.append(offset)
            offset += 16 + struct.unpack_from("<I", stream, offset)[0]
        before = stream[:boundaries[3]]
        after = stream[boundaries[3]:]
        origin = struct.pack("<IIQ", 0, 8, 500)
        variants = {
            "before_configuration": origin + stream,
            "duplicate": before + origin + origin + after,
            "mixed_flags": before + struct.pack("<IIQ", 0, 9, 500) + after,
            "payload": before + struct.pack("<IIQ", 1, 8, 500) + b"x" + after,
            "overflow": before + struct.pack("<IIQ", 0, 8, 1 << 32) + after,
            "after_audio": stream[:boundaries[4]] + origin + stream[boundaries[4]:],
        }
        for name, malformed in variants.items():
            with self.subTest(name=name):
                machine, blocks = decode(self.program, malformed, 2)
                self.assertEqual(3, blocks[-1][0])
                self.assertEqual(0, machine.word(0xE000ED28))
                self.assertEqual(0, machine.word(0xE000ED2C))
                self.assertEqual(bytes(machine.uc.mem_read(0x0102A000, 64)), bytes([0xa5]) * 64)

    def test_cropped_first_page_matches_independent_reference_samples_and_positions(self):
        self.check_reference("cropped44", 2)

    def test_mono_speech_matches_the_reference_and_final_granule(self):
        self.check_reference("mono8", 1)

    def test_independent_stereo_channels_match_the_reference(self):
        self.check_reference("stereo44", 2)

    def test_high_quality_stereo_fits_and_matches_the_reference(self):
        self.check_reference("stereo48", 2)

    def test_only_high_rate_vorbis_requests_the_200_mhz_clock(self):
        for rate in (48000, 96000, 176400, 192000):
            stream = bytearray(packets("stereo48"))
            struct.pack_into("<I", stream, 16 + 12, rate)
            machine, blocks = decode(self.program, bytes(stream), 2)
            self.assertEqual(core1.RESULT_END, blocks[-1][0])
            expected = set() if rate <= 96000 else {core1.CLOCK_100_MHZ, core1.CLOCK_200_MHZ}
            self.assertEqual(expected, set(machine.clock_writes))
            machine.set_control("generation", 0)
            for _ in range(10):
                machine.run_slice()
            self.assertEqual(core1.CLOCK_100_MHZ, machine.clock)

    def test_low_quality_stereo_fits_and_matches_the_reference(self):
        self.check_reference("low48", 2)

    def test_headers_and_packets_can_wrap_across_the_input_ring(self):
        self.check_reference("stereo44", 2, start=0xFFDF)

    def test_workspace_exhaustion_leaves_through_the_decoder_failure_path(self):
        stream = bytearray(packets("stereo44"))
        at = 0
        for _ in range(2):
            length = struct.unpack_from("<I", stream, at)[0]
            at += 16 + length
        setup = at + 16
        self.assertEqual(b"\x42\x43\x56", stream[setup + 8:setup + 11])
        stream[setup + 13:setup + 16] = b"\x00\x00\x01"
        machine, blocks = decode(self.program, bytes(stream), 2)
        self.assertEqual(3, blocks[-1][0])
        self.assertEqual(0, sum(block[2] for block in blocks))
        self.assertEqual(0, machine.word(0xE000ED28))
        self.assertEqual(0, machine.word(0xE000ED2C))

    def test_a_track_change_during_decode_publishes_only_the_new_generation(self):
        machine = core1.Core1(self.program, 65536)
        self.assertTrue(machine.boot())
        machine.start_stream(2, 24, 0)
        stream = packets("stereo44")
        replacement = packets("mono8")
        fed = 0
        switched = False
        blocks = []
        symbols = load_symbols(Path(ARGUMENTS["program"]).with_name("echo_core1_vorbis.elf"))
        address = symbols["vorbis_decoder_decode"]

        def switch(uc, where, size, data):
            nonlocal stream, fed, switched
            if switched:
                return
            switched = True
            stream = replacement
            uc.mem_write(core1.RING, stream + bytes(65536 - len(stream)))
            fed = len(stream)
            machine.start_stream(1, 24, 0)
            machine.set_control("written", fed)
            machine.set_control("generation", 2)

        machine.uc.hook_add(UC_HOOK_BLOCK, switch, begin=address, end=address)
        for _ in range(200_000):
            fed = machine.feed(stream, fed)
            machine.run_slice()
            blocks.extend(machine.take_slots(machine.control("channels")))
            if blocks and blocks[-1][0] != core1.RESULT_READY:
                break
        self.assertTrue(switched)
        self.assertEqual(core1.RESULT_END, blocks[-1][0])
        raw = (FIXTURES / "mono8.pcm").read_bytes()
        reference = struct.unpack(f"<{len(raw) // 2}h", raw)
        actual = [value for block in blocks[:-1] for value in block[3][0]]
        self.assertEqual(len(reference), len(actual))
        self.assertTrue(all(abs((value >> 8) - expected) <= 2
                            for value, expected in zip(actual, reference)))
        self.assertEqual(2, machine.control("acked"))

    def test_an_oversized_packet_is_refused_before_copying_into_the_snapshot(self):
        stream = struct.pack("<IIQ", 32769, 1, 0) + bytes(32769)
        machine, blocks = decode(self.program, stream, 2)
        self.assertEqual(3, blocks[-1][0])
        self.assertEqual(0, sum(block[2] for block in blocks))
        self.assertEqual(bytes(machine.uc.mem_read(0x0102A000, 64)), b"\xa5" * 64)

    def test_late_setup_truncation_fails_without_dereferencing_cleanup_garbage(self):
        original = packets("stereo44")
        at = 0
        for _ in range(2):
            at += 16 + struct.unpack_from("<I", original, at)[0]
        length = struct.unpack_from("<I", original, at)[0]
        clipped = length - 30
        stream = bytearray(original[:at + 16 + clipped])
        struct.pack_into("<I", stream, at, clipped)
        _, blocks = decode(self.program, bytes(stream), 2)
        self.assertEqual(3, blocks[-1][0])
        self.assertEqual(0, sum(block[2] for block in blocks))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", required=True)
    parser.add_argument("--tool", required=True)
    arguments, remaining = parser.parse_known_args()
    if not Path(arguments.program).exists() or not Path(arguments.tool).exists():
        sys.exit(77)
    ARGUMENTS.update(program=arguments.program, tool=arguments.tool)
    unittest.main(argv=[sys.argv[0], *remaining])
