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
        echo_faces = config["faces"][1:7]
        self.assertEqual(["inter_regular_22", "inter_semibold_22", "inter_regular_18", "inter_semibold_18",
                          "inter_regular_26", "inter_semibold_26"], [face["name"] for face in echo_faces])
        for face in echo_faces:
            wanted = glyph_pack.face_characters(face)
            self.assertTrue(set(range(32, 127)) <= wanted)
            self.assertTrue(set(range(160, 256)) <= wanted)
            self.assertTrue(set(range(0x400, 0x460)) <= wanted)
            self.assertTrue({0x2013, 0x2019, 0x2026, 0x20AC, 0x2713} <= wanted)

    def test_cjk_faces_contain_every_configured_glyph(self):
        config = json.loads((ASSETS_DIRECTORY / "glyph_pack.json").read_text())
        faces = config["faces"][7:]
        base = [face["name"] for face in config["faces"][:7]]
        self.assertEqual([name + suffix for suffix in ("_sc", "_jp") for name in base],
                         [face["name"] for face in faces])
        for face in faces:
            path = TOOLS_DIRECTORY.parent / "src/fonts/packs" / (face["name"] + ".bin")
            glyphs = glyph_pack.read_face(path.read_bytes())
            self.assertEqual(glyph_pack.face_characters(face), glyphs.keys())

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
        self.assertEqual(len(config["faces"]), int(constants["FONT_PACK_IMAGE_FACES"]))
        self.assertEqual(glyph_pack.EXTRA_FACES, int(constants["FONT_PACK_EXTRA_FACES"]))
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
        count, size, first, last, roots = struct.unpack_from("<IIHHH", blob, 8)
        self.assertEqual((b"EGLF", 1, 12, 42), (magic, version, entry, per_sector))
        self.assertEqual((50, len(blob), 0xE000, 0xE000 + 49, 1), (count, size, first, last, roots))
        self.assertEqual(0xE000, struct.unpack_from("<H", blob, glyph_pack.ROOT_OFFSET)[0])
        self.assertEqual(0xE000 + 42, struct.unpack_from("<H", blob, 512 + 2)[0])
        self.assertEqual(0xE000 + 42, struct.unpack_from("<H", blob, 1024 + 512)[0])
        self.assertEqual(glyph_pack.fnv(blob[:40]), struct.unpack_from("<I", blob, 40)[0])

    def test_the_header_names_the_firmware_constants(self):
        header = (TOOLS_DIRECTORY.parent / "src" / "gfx" / "include" / "gfx" / "font_pack.h").read_text()
        constants = dict(re.findall(r"(FONT_PACK_\w+) = (\w+)", header))
        self.assertEqual(glyph_pack.HEADER_BYTES, int(constants["FONT_PACK_HEADER_BYTES"]))
        self.assertEqual(glyph_pack.HEADER_CHECK, int(constants["FONT_PACK_HEADER_CHECK"]))
        self.assertEqual(glyph_pack.ROOT_OFFSET, int(constants["FONT_PACK_ROOT_OFFSET"]))
        self.assertEqual(glyph_pack.CHECK_OFFSET, int(constants["FONT_PACK_SECTOR_CHECK"]))
        self.assertEqual(glyph_pack.DIRECTORY_ENTRIES, int(constants["FONT_PACK_DIRECTORY_ENTRIES"]))
        self.assertEqual(glyph_pack.MAX_ROOT, int(constants["FONT_PACK_MAX_ROOT"]))
        self.assertEqual(glyph_pack.MAX_GLYPHS, int(constants["FONT_PACK_MAX_GLYPHS"], 0))

    def test_every_sector_carries_its_check_seeded_by_the_header(self):
        blob = glyph_pack.pack_glyphs([glyph(0x100 + index) for index in range(100)])
        seed = struct.unpack_from("<I", blob, 40)[0]
        for sector in range(1, 5):
            body = blob[sector * 512:(sector + 1) * 512]
            self.assertEqual(glyph_pack.fnv(body[:508], seed), struct.unpack_from("<I", body, 508)[0])

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

    def test_a_face_holds_more_glyphs_than_one_directory_sector_names(self):
        count = glyph_pack.DIRECTORY_ENTRIES * glyph_pack.ENTRIES_PER_SECTOR + 50
        glyphs = [glyph(0x1000 + index, 1, 1, levels=[index % 16]) for index in range(count)]
        blob = glyph_pack.pack_glyphs(glyphs)
        read = glyph_pack.read_face(blob)
        self.assertEqual(count, len(read))
        self.assertEqual(2, struct.unpack_from("<H", blob, 20)[0])
        self.assertEqual(0x1000 + glyph_pack.DIRECTORY_ENTRIES * glyph_pack.ENTRIES_PER_SECTOR,
                         struct.unpack_from("<H", blob, glyph_pack.ROOT_OFFSET + 2)[0])
        self.assertEqual(bytes([(count - 1) % 16 << 4]), read[0x1000 + count - 1][5])

    def test_glyphs_out_of_order_are_refused(self):
        with self.assertRaises(glyph_pack.PackError):
            glyph_pack.pack_glyphs([glyph(0xE001), glyph(0xE000)])
        with self.assertRaises(glyph_pack.PackError):
            glyph_pack.pack_glyphs([glyph(0xE000), glyph(0xE000)])

    def test_codepoints_past_the_basic_plane_are_refused(self):
        with self.assertRaises(glyph_pack.PackError):
            glyph_pack.pack_glyphs([glyph(0x10000)])

    def test_damage_anywhere_before_the_bitmaps_is_refused(self):
        blob = glyph_pack.pack_glyphs([glyph(0xE000 + index) for index in range(50)])
        for offset in (8, 30, 512 + 1, 1024 + 13, 1536 + 507):
            damaged = bytearray(blob)
            damaged[offset] ^= 1
            with self.assertRaises(glyph_pack.PackError):
                glyph_pack.read_face(bytes(damaged))

    def test_a_truncated_face_is_refused(self):
        blob = glyph_pack.pack_glyphs([glyph(0xE000 + index) for index in range(50)])
        for size in (40, 512, 1100, len(blob) - 1):
            with self.assertRaises(glyph_pack.PackError):
                glyph_pack.read_face(blob[:size])

    def test_a_bitmap_larger_than_the_player_holds_is_refused(self):
        glyph_pack.pack_glyphs([glyph(0xE000, 26, 27)])
        with self.assertRaises(glyph_pack.PackError):
            glyph_pack.pack_glyphs([glyph(0xE000, 27, 27)])

    def test_the_bitmap_limit_is_the_players(self):
        header = (TOOLS_DIRECTORY.parent / "src" / "gfx" / "include" / "gfx" / "font_pack.h").read_text()
        constants = dict(re.findall(r"(FONT_PACK_\w+) = (\w+)", header))
        self.assertEqual(glyph_pack.MAX_FACE_BITMAP, int(constants["FONT_PACK_MAX_BITMAP"]))

    def test_metrics_out_of_range_are_refused(self):
        with self.assertRaises(glyph_pack.PackError):
            glyph_pack.pack_glyphs([glyph(0xE000, left=200)])


class ExtraFontTest(unittest.TestCase):
    def face(self, first, count=3):
        return glyph_pack.pack_glyphs([glyph(first + index) for index in range(count)])

    def test_the_container_reads_back(self):
        faces = [self.face(0xAC00), None, self.face(0x4E00, 60), None, None, None, self.face(0x3041)]
        blob = glyph_pack.pack_extra_font(faces, ["zh-TW", "ko"])
        read, languages = glyph_pack.read_extra_font(blob)
        self.assertEqual(["ko", "zh-TW"], languages)
        self.assertEqual([glyph_pack.read_face(face) if face else None for face in faces], read)

    def test_the_container_is_byte_identical_for_the_same_inputs(self):
        faces = [self.face(0xAC00)] + [None] * 6
        self.assertEqual(glyph_pack.pack_extra_font(faces, ["ko", "ja"]),
                         glyph_pack.pack_extra_font(faces, ["ja", "ko", "ja"]))

    def test_the_header_matches_the_player_reader(self):
        faces = [None, self.face(0xAC00)] + [None] * 5
        blob = glyph_pack.pack_extra_font(faces, ["ko"])
        magic, version, count, languages, size = struct.unpack_from("<4sBBBxI", blob)
        self.assertEqual((b"EGXF", 1, 7, 1, len(blob)), (magic, version, count, languages, size))
        self.assertEqual((0, 0), struct.unpack_from("<II", blob, 12))
        self.assertEqual((512, len(faces[1])), struct.unpack_from("<II", blob, 20))
        self.assertEqual(b"ko\0\0\0\0\0\0", blob[68:76])
        self.assertEqual(glyph_pack.fnv(blob[:508]), struct.unpack_from("<I", blob, 508)[0])
        self.assertEqual(0, len(blob) % 512)

    def test_faces_start_on_sectors(self):
        faces = [self.face(0xAC00, count) for count in (1, 43, 2, 90, 5, 6, 7)]
        blob = glyph_pack.pack_extra_font(faces, [])
        offsets = [struct.unpack_from("<II", blob, 12 + 8 * face)[0] for face in range(7)]
        self.assertTrue(all(offset % 512 == 0 for offset in offsets))

    def test_bad_inputs_are_refused(self):
        for faces, languages in (([None] * 6, []), ([None] * 7, ["toolong"]), ([None] * 7, ["k o"]),
                                 ([None] * 7, [f"l{index}" for index in range(33)]),
                                 ([b"EGLF"] + [None] * 6, [])):
            with self.assertRaises(glyph_pack.PackError):
                glyph_pack.pack_extra_font(faces, languages)

    def test_a_damaged_or_truncated_container_is_refused(self):
        blob = glyph_pack.pack_extra_font([self.face(0xAC00)] + [None] * 6, ["ko"])
        for damaged in (blob[:-1], blob[:511], blob + bytes(512),
                        blob[:70] + b"x" + blob[71:], blob[:600] + b"x" + blob[601:]):
            with self.assertRaises(glyph_pack.PackError):
                glyph_pack.read_extra_font(damaged)

    def test_the_extra_faces_follow_the_base_faces(self):
        config = json.loads((ASSETS_DIRECTORY / "glyph_pack.json").read_text())
        self.assertEqual(7, glyph_pack.EXTRA_FACES)
        self.assertEqual(["inter_regular_16_latin1", "inter_regular_22", "inter_semibold_22",
                          "inter_regular_18", "inter_semibold_18", "inter_regular_26",
                          "inter_semibold_26"],
                         [face["name"] for face in config["faces"][:glyph_pack.EXTRA_FACES]])


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
