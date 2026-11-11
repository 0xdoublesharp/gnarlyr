import json
import re
import struct
import sys
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
ASSETS_DIRECTORY = TOOLS_DIRECTORY.parent / "assets" / "fonts"
sys.path.insert(0, str(TOOLS_DIRECTORY))

import font_build
import glyph_pack


def glyph(codepoint, width=2, height=2, left=0, top=2, advance=3, levels=None):
    levels = levels if levels is not None else [15] * (width * height)
    return font_build.GlyphImage(codepoint, width, height, left, top, advance, levels)


class CharacterSetTest(unittest.TestCase):
    def test_a_face_asks_for_its_ranges(self):
        self.assertEqual({0x41, 0x42, 0x43, 0x61}, glyph_pack.face_characters({"ranges": [[0x41, 0x43], [0x61, 0x61]]}))

    def test_the_latin1_faces_hold_the_accented_letters(self):
        config = json.loads((ASSETS_DIRECTORY / "glyph_pack.json").read_text())
        for face in config["faces"][:1]:
            self.assertTrue(set(range(160, 256)) | set(range(1024, 1120)) <= glyph_pack.face_characters(face))

    def test_small_face_contains_cyrillic_bitmaps(self):
        path = TOOLS_DIRECTORY.parent / "src/fonts/packs/inter_regular_16_latin1.bin"
        glyphs = glyph_pack.read_face(path.read_bytes())
        self.assertTrue(set(range(0x400, 0x460)) <= glyphs.keys())
        self.assertTrue({0x490, 0x491} <= glyphs.keys())

    def test_the_echo_faces_hold_ascii_latin1_and_punctuation(self):
        config = json.loads((ASSETS_DIRECTORY / "glyph_pack.json").read_text())
        echo_faces = config["faces"][1:]
        self.assertEqual(["inter_regular_22", "inter_semibold_22", "inter_regular_18", "inter_semibold_18",
                          "inter_regular_26", "inter_semibold_26"], [face["name"] for face in echo_faces])
        for face in echo_faces:
            wanted = glyph_pack.face_characters(face)
            self.assertTrue(set(range(32, 127)) <= wanted)
            self.assertTrue(set(range(160, 256)) <= wanted)
            self.assertTrue(set(range(0x400, 0x460)) <= wanted)
            self.assertTrue({0x2013, 0x2019, 0x2026, 0x20AC, 0x2713} <= wanted)

    def test_the_echo_faces_may_hold_larger_glyphs(self):
        config = json.loads((ASSETS_DIRECTORY / "glyph_pack.json").read_text())
        limits = {face["name"]: face.get("max_bitmap", glyph_pack.MAX_BITMAP_BYTES)
                  for face in config["faces"]}
        self.assertEqual(256, limits["inter_regular_22"])
        self.assertEqual(128, limits["inter_regular_16_latin1"])
        self.assertEqual(320, limits["inter_semibold_26"])

    def test_the_player_reads_every_face_and_its_largest_glyph(self):
        config = json.loads((ASSETS_DIRECTORY / "glyph_pack.json").read_text())
        header = (TOOLS_DIRECTORY.parent / "src" / "gfx" / "include" / "gfx" / "font_pack.h").read_text()
        constants = dict(re.findall(r"(FONT_PACK_\w+) = (\d+)", header))
        self.assertLessEqual(len(config["faces"]), int(constants["FONT_PACK_FACES"]))
        for face in config["faces"]:
            limit = face.get("max_bitmap", glyph_pack.MAX_BITMAP_BYTES)
            self.assertLessEqual(limit, int(constants["FONT_PACK_MAX_BITMAP"]))

    def test_faces_are_numbered_from_one_in_config_order(self):
        config = {"faces": [{"name": "a"}, {"name": "b"}]}
        self.assertEqual({"a": 1, "b": 2}, glyph_pack.face_numbers(config))


class PackTest(unittest.TestCase):
    def test_a_packed_face_reads_back(self):
        glyphs = [glyph(0xE000 + index, levels=[index % 16, 15, 0, 7]) for index in range(100)]
        blob = glyph_pack.pack_glyphs(glyphs)
        read = glyph_pack.read_face(blob)
        self.assertEqual(100, len(read))
        self.assertEqual((2, 2, 0, 2, 3, bytes([0x5F, 0x07])), read[0xE005])

    def test_the_header_matches_the_player_reader(self):
        blob = glyph_pack.pack_glyphs([glyph(0xE000 + index) for index in range(50)])
        magic, version, entry, per_sector = struct.unpack_from("<4sBBH", blob)
        count, directory, index = struct.unpack_from("<III", blob, 8)
        self.assertEqual((b"EGLF", 1, 12, 42), (magic, version, entry, per_sector))
        self.assertEqual((50, 512, 1024), (count, directory, index))
        self.assertEqual(0xE000 + 42, struct.unpack_from("<H", blob, directory + 2)[0])
        self.assertEqual(0xE000 + 42, struct.unpack_from("<H", blob, index + 512)[0])

    def test_entries_fill_whole_sectors(self):
        self.assertEqual(42, glyph_pack.ENTRIES_PER_SECTOR)
        self.assertEqual(12, glyph_pack.ENTRY.size)

    def test_a_blank_glyph_has_no_bitmap(self):
        blob = glyph_pack.pack_glyphs([glyph(0xA0, 0, 0, levels=[]), glyph(0xA1)])
        read = glyph_pack.read_face(blob)
        self.assertEqual(b"", read[0xA0][5])
        self.assertEqual(bytes([0xFF, 0xFF]), read[0xA1][5])

    def test_a_corrupted_face_is_refused(self):
        blob = bytearray(glyph_pack.pack_glyphs([glyph(0xE000)]))
        blob[-1] ^= 1
        with self.assertRaises(glyph_pack.PackError):
            glyph_pack.read_face(bytes(blob))

    def test_an_empty_face_is_refused(self):
        with self.assertRaises(glyph_pack.PackError):
            glyph_pack.pack_glyphs([])

    def test_too_many_glyphs_are_refused(self):
        count = glyph_pack.MAX_DIRECTORY * glyph_pack.ENTRIES_PER_SECTOR + 1
        with self.assertRaises(glyph_pack.PackError):
            glyph_pack.pack_glyphs([glyph(index, 0, 0, levels=[]) for index in range(count)])

    def test_metrics_out_of_range_are_refused(self):
        with self.assertRaises(glyph_pack.PackError):
            glyph_pack.pack_glyphs([glyph(0xE000, left=200)])


class BuildFaceTest(unittest.TestCase):
    def test_only_characters_in_the_font_are_packed(self):
        spec = {"name": "latin", "file": "inter/Inter-Regular.ttf", "size": 16}
        face = glyph_pack.build_face(spec, {ord("A"), ord("B"), 0x0E01}, ASSETS_DIRECTORY)
        read = glyph_pack.read_face(face.blob)
        self.assertEqual({ord("A"), ord("B")}, set(read))
        self.assertEqual(2, face.glyph_count)
        self.assertEqual([], face.dropped)

    def test_rendering_matches_the_built_in_fonts(self):
        spec = {"name": "latin", "file": "inter/Inter-Regular.ttf", "size": 16}
        face = glyph_pack.build_face(spec, {ord("H")}, ASSETS_DIRECTORY)
        font = font_build.load_font(ASSETS_DIRECTORY / spec["file"], 16)
        expected = font_build.render_glyph(font, ord("H"))
        width, height, left, top, advance, bitmap = glyph_pack.read_face(face.blob)[ord("H")]
        self.assertEqual((expected.width, expected.height, expected.left, expected.top,
                          expected.advance), (width, height, left, top, advance))
        self.assertEqual(font_build.pack_nibbles(expected.levels), bitmap)


if __name__ == "__main__":
    unittest.main()
