import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

SPEC = importlib.util.spec_from_file_location("site_screenshots", Path(__file__).parents[1] / "tools/site_screenshots.py")
shots = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(shots)
SCRATCH = Path("/Volumes/Scratch/SnowskyEcho/tmp")


def bitmap(height=2):
    pixels = b"\xff\x00\x00\x00\xff\x00\x00\x00" + b"\x00\x00\xff\xff\xff\xff\x00\x00"
    if height < 0:
        pixels = pixels[8:] + pixels[:8]
    header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, 2, height, 1, 24, 0, len(pixels), 0, 0, 0, 0)
    return header + pixels


def scanlines(png):
    position = 8
    data = b""
    while position < len(png):
        length, kind = struct.unpack_from(">I4s", png, position)
        if kind == b"IDAT":
            data += png[position + 8:position + 8 + length]
        position += length + 12
    return zlib.decompress(data)


class ScreenshotConfigTests(unittest.TestCase):
    def config(self, cases):
        self.assertTrue(Path("/Volumes/Scratch").is_mount(), "Mount Scratch before these checks")
        SCRATCH.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="docs-screen-test-", dir=SCRATCH) as folder:
            path = Path(folder) / "config.json"
            path.write_text(json.dumps({"version": 1, "defaults": {"frames": 400}, "screens": cases}))
            return shots.load_config(path)

    def test_output_paths_cannot_escape_the_asset_directory(self):
        for filename in ("../outside.png", "/outside.png", "nested/view.png", "view.svg"):
            with self.subTest(filename=filename), self.assertRaises(ValueError):
                self.config([{"name": "home", "file": filename, "expected_title": "Home", "keys": []}])

    def test_duplicate_outputs_are_rejected_before_rendering(self):
        with self.assertRaises(ValueError):
            self.config([{"name": "home", "file": "home.png", "expected_title": "Home", "keys": []}] * 2)

    def test_capture_timing_and_buttons_are_bounded(self):
        for keys, frames in ((["shell"], 400), (["play;whoami"], 400), (["up"] * 65, 400), ([], 0), ([], 20001)):
            with self.subTest(keys=keys, frames=frames), self.assertRaises(ValueError):
                self.config([{"name": "home", "file": "home.png", "expected_title": "Home", "keys": keys, "frames": frames}])

    def test_native_and_theme_sources_cannot_escape_the_capture_inputs(self):
        for field, value in (("native_snapshot", "../outside.ppm"),
                             ("native_snapshot", "/outside.ppm"),
                             ("theme_example", "../outside")):
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                self.config([{"name": "home", "file": "home.png", "expected_title": "Home",
                              field: value}])

    def test_language_scenes_reject_unsafe_codes_and_missing_translations(self):
        for language in ({"code": "../es", "strings": {"STR_MUSIC": "Música"}},
                         {"code": "es", "strings": {}},
                         {"code": "es", "strings": {"STR_MUSIC": "Music\nSettings"}}):
            with self.subTest(language=language), self.assertRaises(ValueError):
                self.config([{"name": "home-es", "file": "home-es.png", "expected_title": "Home",
                              "language": language}])


class ScreenshotPixelTests(unittest.TestCase):
    def test_bottom_up_and_top_down_bmp_preserve_exact_rgb_pixels(self):
        expected = b"\x00\xff\x00\x00\xff\xff\xff\x00\x00\x00\xff\x00\xff\x00"
        for height in (2, -2):
            with self.subTest(height=height):
                png = shots.bmp_to_png(bitmap(height))
                self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
                self.assertEqual(scanlines(png), expected)

    def test_native_ppm_captures_preserve_pixels_and_reject_truncation(self):
        ppm = b"P6\n2 1\n255\n" + b"\xff\x00\x00\x00\xff\x00"
        self.assertEqual(scanlines(shots.ppm_to_png(ppm)), b"\0\xff\0\0\0\xff\0")
        for data in (ppm[:-1], b"P6\n99999 1\n255\n", b"P3\n2 1\n255\n"):
            with self.subTest(data=data), self.assertRaises(ValueError):
                shots.ppm_to_png(data)

    def test_bad_or_truncated_screenshots_are_rejected(self):
        for data in (b"", bitmap()[:-1], bitmap().replace(b"BM", b"ZZ", 1)):
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                shots.bmp_to_png(data)


if __name__ == "__main__":
    unittest.main()
