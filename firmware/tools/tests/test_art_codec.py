import random
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import art_codec
from art_fixtures import flat, noise, overlay, photo

TOOL = None
if "--tool" in sys.argv:
    at = sys.argv.index("--tool")
    TOOL = sys.argv[at + 1]
    del sys.argv[at:at + 2]


def under(x, y):
    return ((x * 37 + y * 101) * 2654435761 & 0xFFFFFFFF) >> 16


def under_rows(width, height):
    return [[under(x, y) for x in range(width)] for y in range(height)]


class ArtCodecTests(unittest.TestCase):
    def run_tool(self, data, width, height, first, last):
        if TOOL is None:
            self.skipTest("C decoder is supplied by CTest")
        with tempfile.NamedTemporaryFile(suffix=".art") as file:
            file.write(data)
            file.flush()
            result = subprocess.run([TOOL, file.name, str(width), str(height), str(first),
                                     str(last)], capture_output=True, check=False)
        if result.returncode == 2:
            self.fail(result.stderr)
        if result.returncode:
            return None
        values = struct.unpack("<%dH" % (len(result.stdout) // 2), result.stdout)
        span = last - first
        return [list(values[y * span:(y + 1) * span]) for y in range(height)]

    def encoded(self, image, codec, dither="none"):
        pixels = art_codec.prepared(image, codec, dither)
        return pixels, art_codec.payload(pixels, codec)

    def assert_parity(self, data, width, height, spans=None):
        reference = art_codec.decode(data, width, height, under_rows(width, height))
        for first, last in spans or [(0, width)]:
            decoded = self.run_tool(data, width, height, first, last)
            self.assertIsNotNone(decoded, (first, last))
            self.assertEqual([row[first:last] for row in reference], decoded, (first, last))
        return reference

    def test_flat_art_round_trips_exactly_with_both_codecs(self):
        image = flat(480, 222)
        for codec in (art_codec.INDEXED, art_codec.LOSSLESS):
            with self.subTest(codec=codec):
                pixels, data = self.encoded(image, codec)
                decoded = self.assert_parity(data, 480, 222)
                self.assertEqual([[color for color, _ in row] for row in pixels], decoded)

    def test_photos_decode_identically_in_c_and_python(self):
        image = photo(222, 480)
        for codec, dither in ((art_codec.INDEXED, "fs"), (art_codec.INDEXED, "none"),
                              (art_codec.LOSSLESS, "none"), (art_codec.LOSSLESS, "ordered")):
            with self.subTest(codec=codec, dither=dither):
                pixels, data = self.encoded(image, codec, dither)
                decoded = self.assert_parity(data, 222, 480)
                if codec == art_codec.LOSSLESS:
                    self.assertEqual([[color for color, _ in row] for row in pixels], decoded)

    def test_alpha_overlays_composite_identically(self):
        image = overlay(160, 48)
        background = under_rows(160, 48)
        for codec in (art_codec.INDEXED, art_codec.LOSSLESS):
            with self.subTest(codec=codec):
                pixels, data = self.encoded(image, codec)
                decoded = self.assert_parity(data, 160, 48)
                expected = [[art_codec.put(background[y][x], color, alpha)
                             for x, (color, alpha) in enumerate(row)]
                            for y, row in enumerate(pixels)]
                self.assertEqual(expected, decoded)

    def test_clipped_spans_match_the_full_row(self):
        for image, codec in ((photo(480, 40), art_codec.INDEXED), (photo(480, 40), art_codec.LOSSLESS),
                             (overlay(480, 40), art_codec.LOSSLESS)):
            with self.subTest(codec=codec, mode=image.mode):
                _, data = self.encoded(image, codec, "fs" if codec == art_codec.INDEXED else "none")
                self.assert_parity(data, 480, 40, [(0, 480), (136, 466), (300, 400), (0, 1),
                                                   (479, 480), (127, 129), (240, 240)])

    def test_widths_around_packet_limits_decode_identically(self):
        for width in (1, 2, 31, 32, 33, 63, 64, 65, 127, 128, 129, 222, 256):
            for codec in (art_codec.INDEXED, art_codec.LOSSLESS):
                with self.subTest(width=width, codec=codec):
                    image = noise(width, 3) if codec == art_codec.LOSSLESS else flat(width, 3)
                    _, data = self.encoded(image, codec)
                    self.assert_parity(data, width, 3, [(0, width), (width // 2, width)])

    def test_encoded_rows_stay_within_their_bounds(self):
        for width in (64, 222, 480):
            _, indexed = self.encoded(noise(width, 2), art_codec.INDEXED)
            info = art_codec.parse(indexed, width, 2)
            sizes = [b - a for a, b in zip(info["table"], info["table"][1:])]
            self.assertLessEqual(max(sizes), width + (width + 127) // 128)
            _, lossless = self.encoded(noise(width, 2), art_codec.LOSSLESS)
            info = art_codec.parse(lossless, width, 2)
            sizes = [b - a for a, b in zip(info["table"], info["table"][1:])]
            self.assertLessEqual(max(sizes), art_codec.ROW_MAX_BYTES)

    def test_a_row_over_1024_bytes_is_refused_by_the_encoder(self):
        image = Image.new("RGBA", (480, 1))
        image.putdata([(x % 256, 255 - x % 256, 7, 20 + (x % 2) * 120) for x in range(480)])
        pixels = art_codec.prepared(image, art_codec.LOSSLESS, "none")
        with self.assertRaises(ValueError):
            art_codec.payload(pixels, art_codec.LOSSLESS)

    def test_malformed_rows_are_refused_like_the_reference(self):
        rng = random.Random(11)
        for image, codec in ((flat(64, 4), art_codec.INDEXED), (overlay(64, 4), art_codec.INDEXED),
                             (photo(64, 4), art_codec.LOSSLESS),
                             (overlay(64, 4), art_codec.LOSSLESS)):
            _, data = self.encoded(image, codec)
            info = art_codec.parse(data, 64, 4)
            start = info["table"][0]
            for _ in range(60):
                broken = bytearray(data)
                at = rng.randrange(start, len(data))
                broken[at] = rng.randrange(256)
                with self.subTest(codec=codec, at=at, value=broken[at]):
                    try:
                        expected = self.reference_rows(bytes(broken), info)
                    except ValueError:
                        expected = None
                    self.assertEqual(expected, self.run_tool(bytes(broken), 64, 4, 0, 64))

    def reference_rows(self, data, info):
        table, rows = info["table"], []
        background = under_rows(info["width"], info["height"])
        for y in range(info["height"]):
            row = data[table[y]:table[y + 1]]
            if info["codec"] == art_codec.INDEXED:
                pixels, used = art_codec.decode_indexed_row(row, info["width"], info["colors"],
                                                            info["alphas"], background[y])
            else:
                pixels, used = art_codec.decode_lossless_row(row, info["width"], background[y],
                                                             bool(info["alpha"]))
            if used != len(row):
                raise ValueError("trailing bytes in a row")
            rows.append(pixels)
        return rows


if __name__ == "__main__":
    unittest.main()
