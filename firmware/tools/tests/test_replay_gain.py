import decimal
import math
import struct
import sys
import unittest
from dataclasses import replace
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import library_index
import replay_gain
from library_fixture import FIXTURE_TRACKS


class ReplayGainTest(unittest.TestCase):
    def test_valid_tags_and_peak_limits_are_retained_by_track_metadata(self):
        tags = {"replaygain_track_gain": ["bad", "6 dB", "0 dB"],
                "replaygain_album_gain": ["0 dB"],
                "replaygain_track_peak": ["2"], "replaygain_album_peak": ["0.5"]}
        track = library_index.track_from_tags("track.flac", tags, 60)
        gain = track.replay_gain
        self.assertAlmostEqual(1, gain[0] / (65536 * math.pow(10, 6 / 20)), delta=0.0001)
        self.assertEqual((65536, 32768, 65536), gain[1:])

    def test_missing_and_invalid_tags_stay_missing(self):
        self.assertEqual((0, 0, 0, 0), replay_gain.from_tags({}))
        for value in ("NaN", "96.01", "1 dB junk", "1\x002", "1e100"):
            with self.subTest(value=value):
                self.assertEqual((0, 0, 0, 0), replay_gain.from_tags(
                    {"replaygain_track_gain": [value], "replaygain_track_peak": ["1"]}))

    def test_all_amplitudes_match_independent_exponentiation(self):
        for value in range(-9600, 9601):
            expected = 65536 * math.pow(10, value / 2000)
            self.assertLessEqual(abs(replay_gain.amplitude(value) - expected),
                                 max(2, expected * 0.0001))

    def test_decimal_peaks_round_up_at_exact_binary_boundaries(self):
        value = 0
        with decimal.localcontext() as context:
            context.prec = 80
            for _ in range(2048):
                exact = decimal.Decimal(value) / (1 << 24)
                self.assertEqual(value, replay_gain.parse_number(str(exact), False))
                if value != (1 << 32) - 1:
                    above = exact + decimal.Decimal("1e-40")
                    self.assertEqual(value + 1, replay_gain.parse_number(str(above), False))
                value = (value * 1664525 + 1013904223) & ((1 << 32) - 1)

    def test_optional_extension_keeps_legacy_bytes_and_tracks_sorted_gains(self):
        base = library_index.encode_library(library_index.build_library(FIXTURE_TRACKS))
        gains = (100000, 80000, 50000, 40000)
        tagged = [replace(track, replay_gain=gains) for track in FIXTURE_TRACKS]
        data = library_index.encode_library(library_index.build_library(tagged))
        self.assertEqual(base, data[:len(base)])
        self.assertEqual(len(base) + 20 + 20 * len(tagged), len(data))
        magic, version, size, count, fingerprint, checksum = struct.unpack_from(
            "<4sHHIII", data, len(base))
        self.assertEqual((b"DSRG", 1, 20, len(tagged)), (magic, version, size, count))
        self.assertNotEqual(0, fingerprint)
        self.assertNotEqual(0, checksum)
        self.assertEqual(gains, struct.unpack_from("<IIII", data, len(base) + 20))


if __name__ == "__main__":
    unittest.main()
