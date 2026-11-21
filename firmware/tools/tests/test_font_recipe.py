import contextlib
import hashlib
import io
import json
import os
import re
import shutil
import struct
import sys
import tempfile
import unicodedata
import unittest
import urllib.error
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import font_build
import font_recipe
import glyph_pack
import lang_pack
import library_index

FONTS = TOOLS.parent / "assets" / "fonts"
NOTO = FONTS / "noto-cjk"
PACKS = TOOLS.parent / "src" / "fonts" / "packs"
STEMS = {"sc": "sc", "tc": "sc", "jp": "jp", "kr": "sc"}
CATALOGUE = lang_pack.read_catalogue()
SEGMENTS = lang_pack.read_segments(CATALOGUE)
TEXT_ID = next(name for name, _ in CATALOGUE if name != lang_pack.NAME_ID)
TEST_RECIPE = None


def reader_limit():
    header = (TOOLS.parent / "src" / "gfx" / "include" / "gfx" / "font_pack.h").read_text(encoding="utf-8")
    return int(re.search(r"\bFONT_PACK_MAX_BITMAP = (\d+)", header).group(1))


def subset_source(stem, weight):
    name = f"{stem}-{weight.capitalize()}.otf"
    data = (NOTO / name).read_bytes()
    return {"file": name, "url": f"https://fonts.invalid/{name}",
            "sha256": hashlib.sha256(data).hexdigest(), "size": len(data), "font_number": 0}


def test_recipe():
    global TEST_RECIPE
    if TEST_RECIPE is None:
        recipe = json.loads(font_recipe.RECIPE.read_text(encoding="utf-8"))
        recipe["sources"] = {style: {weight: subset_source(stem, weight) for weight in font_recipe.WEIGHTS}
                             for style, stem in STEMS.items()}
        recipe["coverage"] = font_recipe.compute_coverage(recipe, font_recipe.Options(NOTO))
        TEST_RECIPE = font_recipe.check_recipe(recipe)
    return json.loads(json.dumps(TEST_RECIPE))


def test_setup():
    fonts = json.loads((FONTS / "fonts.json").read_text(encoding="utf-8"))
    packs = json.loads((FONTS / "glyph_pack.json").read_text(encoding="utf-8"))
    return font_recipe.Setup(test_recipe(), font_recipe.base_drawable(fonts, packs))


def covered_text(count, skip=0):
    coverage = sorted(font_recipe.range_set(test_recipe()["coverage"]))
    return "".join(chr(codepoint) for codepoint in coverage if codepoint >= 0x3000)[skip:skip + count]


def pack(code, name, text):
    return lang_pack.build_pack(code, CATALOGUE, {"STR_LANGUAGE_NAME": name, TEXT_ID: text}, SEGMENTS)


def library(*texts):
    strings = b"\0" + b"".join(text.encode("utf-8") + b"\0" for text in texts)
    header = bytearray(library_index.HEADER_SIZE)
    library_index.HEADER_PREFIX.pack_into(header, 0, library_index.MAGIC, library_index.VERSION, 10)
    library_index.SECTION_ENTRY.pack_into(header, library_index.HEADER_PREFIX.size + 9 * 12,
                                          library_index.HEADER_SIZE, len(strings), 1, 0)
    return bytes(header) + strings


class Volume:
    def __init__(self, test):
        directory = tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR"))
        test.addCleanup(directory.cleanup)
        self.internal = Path(directory.name) / "internal"
        self.card = Path(directory.name) / "card"
        for root in (self.internal, self.card):
            (root / font_recipe.DATA_FOLDER).mkdir(parents=True)
        self.lang = self.card / font_recipe.DATA_FOLDER / "lang"
        self.lang.mkdir()
        self.fonts = self.internal / font_recipe.DATA_FOLDER / "fonts"
        self.volumes = font_recipe.Volumes(self.internal, self.card)

    def add_pack(self, code, name, text):
        (self.lang / f"{code}.pack").write_bytes(pack(code, name, text))

    def add_library(self, *texts):
        (self.card / font_recipe.DATA_FOLDER / "library.db").write_bytes(library(*texts))

    def add_missing(self, text):
        self.fonts.mkdir(exist_ok=True)
        (self.fonts / "missing.txt").write_text(text, encoding="utf-8")

    def settings(self, code):
        data = bytearray(78)
        data[:4] = b"DSST"
        data[19:19 + len(code)] = code.encode("ascii")
        (self.internal / font_recipe.DATA_FOLDER / "settings.bin").write_bytes(bytes(data))


def options():
    return font_recipe.Options(fonts_dir=NOTO)


class RecipeFileTest(unittest.TestCase):
    def setUp(self):
        self.recipe = font_recipe.load_recipe()
        self.faces = json.loads((FONTS / "glyph_pack.json").read_text(encoding="utf-8"))["faces"]

    def test_the_recipe_pins_one_collection_per_weight_for_every_style(self):
        for style in font_recipe.STYLES:
            regular, bold = (self.recipe["sources"][style][weight] for weight in font_recipe.WEIGHTS)
            self.assertNotEqual(regular["sha256"], bold["sha256"])
            for source in (regular, bold):
                self.assertTrue(source["url"].startswith("https://raw.githubusercontent.com/notofonts/noto-cjk/"
                                                         + self.recipe["commit"] + "/"))
                self.assertTrue(source["url"].endswith("/" + source["file"]))
        numbers = {style: self.recipe["sources"][style]["regular"]["font_number"] for style in font_recipe.STYLES}
        self.assertEqual(len(set(numbers.values())), len(numbers))

    def test_faces_render_with_the_images_cjk_parameters(self):
        self.assertEqual(glyph_pack.EXTRA_FACES, len(self.recipe["faces"]))
        for base, chinese, japanese, face in zip(self.faces[:7], self.faces[7:14], self.faces[14:21],
                                                 self.recipe["faces"]):
            self.assertEqual(base["name"], face["name"])
            for cjk in (chinese, japanese):
                self.assertEqual(cjk["size"], face["size"])
                self.assertLessEqual(cjk["max_bitmap"], face["max_bitmap"])
                self.assertEqual("bold" if cjk["file"].endswith("-Bold.otf") else "regular", face["weight"])
            self.assertEqual(reader_limit(), face["max_bitmap"])

    def test_coverage_is_drawable_bmp_text_including_hangul_and_the_image_characters(self):
        coverage = font_recipe.range_set(self.recipe["coverage"])
        self.assertTrue(all(0 < codepoint <= 0xFFFF for codepoint in coverage))
        self.assertFalse(any(unicodedata.category(chr(codepoint)) in ("Cc", "Cs") for codepoint in coverage))
        self.assertLessEqual(set(range(0xAC00, 0xD7A4)), coverage)
        for name in ("sc-characters.txt", "jp-characters.txt"):
            wanted = {int(line[2:], 16) for line in (NOTO / name).read_text(encoding="utf-8").split()}
            self.assertLessEqual(wanted, coverage, name)

    def test_coverage_never_claims_a_character_the_subset_faces_cannot_all_draw(self):
        coverage = font_recipe.range_set(test_recipe()["coverage"])
        cmaps = [set(font_recipe.TTFont(str(NOTO / f"{stem}-{weight}.otf"))["cmap"].getBestCmap())
                 for stem in ("sc", "jp") for weight in ("Regular", "Bold")]
        self.assertLessEqual(coverage, set.intersection(*cmaps))
        self.assertTrue(coverage)
        jp_only = set(cmaps[2]) - set(cmaps[0])
        self.assertFalse(coverage & jp_only)

    def test_a_character_some_face_draws_too_large_is_left_out_of_coverage(self):
        recipe = test_recipe()
        recipe["faces"][0]["max_bitmap"] = 40
        coverage = font_recipe.range_set(font_recipe.compute_coverage(recipe, options()))
        full = font_recipe.range_set(test_recipe()["coverage"])
        self.assertLess(coverage, full)
        self.assertIn(ord("一"), coverage)

    def test_malformed_recipes_are_refused(self):
        for change in (lambda recipe: recipe.update(version=2),
                       lambda recipe: recipe["sources"].pop("kr"),
                       lambda recipe: recipe["sources"]["sc"]["bold"].pop("sha256"),
                       lambda recipe: recipe["sources"]["sc"]["bold"].update(url="http://plain"),
                       lambda recipe: recipe["sources"]["sc"]["bold"].update(file="../x"),
                       lambda recipe: recipe["styles"].update(ja="xx"),
                       lambda recipe: recipe["styles"].pop("default"),
                       lambda recipe: recipe["faces"].pop(),
                       lambda recipe: recipe["coverage"].append([0x10000, 0x10001]),
                       lambda recipe: recipe.update(coverage=[[5, 9], [10, 12]])):
            recipe = test_recipe()
            change(recipe)
            with self.assertRaises(font_recipe.RecipeError):
                font_recipe.check_recipe(recipe)

    def test_the_committed_recipe_text_is_what_the_coverage_command_writes(self):
        self.assertEqual(font_recipe.RECIPE.read_text(encoding="utf-8"), font_recipe.recipe_text(self.recipe))


class RenderingTest(unittest.TestCase):
    def test_recipe_faces_match_the_images_chinese_and_japanese_faces_pixel_for_pixel(self):
        recipe = test_recipe()
        faces = json.loads((FONTS / "glyph_pack.json").read_text(encoding="utf-8"))["faces"]
        for style, offset in (("sc", 7), ("jp", 14)):
            for index, spec in enumerate(recipe["faces"]):
                committed = glyph_pack.read_face((PACKS / f"{faces[offset + index]['name']}.bin").read_bytes())
                source = recipe["sources"][style][spec["weight"]]
                glyphs, dropped = font_recipe.render_face(NOTO / source["file"], spec, 0, sorted(committed))
                self.assertFalse(dropped)
                rendered = {glyph.codepoint: (glyph.width, glyph.height, glyph.left, glyph.top, glyph.advance,
                                              font_build.pack_nibbles(glyph.levels)) for glyph in glyphs}
                self.assertEqual(committed, rendered, spec["name"])


class StyleTest(unittest.TestCase):
    def test_the_menu_language_picks_the_style(self):
        recipe = font_recipe.load_recipe()
        for language, style in (("ja", "jp"), ("ja-JP", "jp"), ("ko", "kr"), ("zh", "sc"), ("zh-CN", "sc"),
                                ("zh-TW", "tc"), ("zh-hk", "tc"), ("fr", "sc"), ("", "sc"), (None, "sc")):
            self.assertEqual(style, font_recipe.style_for(recipe, language), language)


class CollectionTest(unittest.TestCase):
    def test_the_base_faces_draw_latin_and_cyrillic_but_not_the_cjk_fallbacks(self):
        drawable = test_setup().drawable
        self.assertLessEqual({ord("A"), ord("é"), ord("Ж"), ord("…")}, drawable)
        self.assertNotIn(ord("中"), drawable)
        self.assertIn(ord("中"), lang_pack.drawable_characters())
        self.assertLess(drawable, lang_pack.drawable_characters())

    def test_packs_library_and_missing_list_are_all_collected(self):
        volume = Volume(self)
        volume.add_pack("zh", "中文", covered_text(3))
        volume.add_library("Song " + covered_text(2, 10), "Folder/" + covered_text(1, 20) + ".flac")
        volume.add_missing(covered_text(1, 30))
        skipped = []
        packs, texts = font_recipe.gather(volume.volumes, font_recipe.Extras(), skipped)
        self.assertEqual(["zh"], list(packs))
        self.assertIn("中文", packs["zh"])
        self.assertIn(covered_text(3), packs["zh"])
        joined = "".join(texts)
        for text in (covered_text(2, 10), covered_text(1, 20), covered_text(1, 30)):
            self.assertIn(text, joined)
        self.assertEqual([], skipped)

    def test_a_pending_pack_and_library_override_what_is_on_the_volume(self):
        volume = Volume(self)
        volume.add_pack("zh", "中文", covered_text(3))
        volume.add_library("Old " + covered_text(1, 40))
        extras = font_recipe.Extras(packs={"zh": pack("zh", "中文", covered_text(2, 50))},
                                    library=library("New " + covered_text(1, 60)))
        packs, texts = font_recipe.gather(volume.volumes, extras, [])
        self.assertIn(covered_text(2, 50), packs["zh"])
        self.assertNotIn(covered_text(3), packs["zh"])
        self.assertIn("New " + covered_text(1, 60), texts)
        self.assertNotIn("Old " + covered_text(1, 40), texts)

    def test_damaged_or_misnamed_packs_and_a_damaged_library_are_skipped_and_reported(self):
        volume = Volume(self)
        (volume.lang / "xx.pack").write_bytes(b"DSLP broken")
        (volume.lang / "de.pack").write_bytes(pack("fr", "Français", "Retour"))
        (volume.lang / ".hidden.pack").write_bytes(b"")
        (volume.lang / "notes.txt").write_text("ignored")
        (volume.card / font_recipe.DATA_FOLDER / "library.db").write_bytes(b"DSDB" + bytes(200))
        skipped = []
        packs, texts = font_recipe.gather(volume.volumes, font_recipe.Extras(), skipped)
        self.assertEqual({}, packs)
        self.assertEqual([], texts)
        self.assertEqual(2, len(skipped))
        self.assertTrue(skipped[0].startswith("xx.pack"))
        self.assertTrue(skipped[1].startswith("library.db"))

    def test_the_library_strings_come_from_the_strings_section(self):
        data = (TOOLS.parent / "tests" / "fixtures" / "small_library.db").read_bytes()
        strings = font_recipe.library_strings(data)
        self.assertTrue(strings)
        self.assertTrue(any("/" in text for text in strings))
        broken = bytearray(library("x"))
        struct.pack_into("<I", broken, library_index.HEADER_PREFIX.size + 9 * 12 + 4, 10_000)
        with self.assertRaises(font_recipe.RecipeError):
            font_recipe.library_strings(bytes(broken))

    def test_the_saved_language_comes_from_settings_and_tolerates_a_bad_file(self):
        volume = Volume(self)
        self.assertIsNone(font_recipe.saved_language(volume.internal))
        volume.settings("ja")
        self.assertEqual("ja", font_recipe.saved_language(volume.internal))
        (volume.internal / font_recipe.DATA_FOLDER / "settings.bin").write_bytes(b"junk")
        self.assertIsNone(font_recipe.saved_language(volume.internal))


class PlanTest(unittest.TestCase):
    def test_built_in_characters_are_subtracted_and_the_rest_sorted(self):
        setup = test_setup()
        cjk = covered_text(4)
        result = font_recipe.plan(setup, {}, [f"Héllo Жук {cjk[::-1]}\n"], "zh")
        self.assertEqual(sorted(ord(character) for character in cjk), result.characters)
        self.assertEqual([], result.unrenderable)
        self.assertEqual("sc", result.style)

    def test_characters_outside_the_coverage_or_the_bmp_are_reported_unrenderable(self):
        result = font_recipe.plan(test_setup(), {}, ["ก🎵" + covered_text(1)], "ko")
        self.assertEqual([ord("ก"), ord("🎵")], result.unrenderable)
        self.assertEqual("kr", result.style)

    def test_only_fully_covered_languages_are_listed(self):
        packs = {"zh": [covered_text(5)], "th": ["ภาษาไทย"], "fr": ["Retour"]}
        self.assertEqual(["fr", "zh"], font_recipe.plan(test_setup(), packs, [], "zh").languages)


class BuildTest(unittest.TestCase):
    def build(self, texts, packs=None, language="zh"):
        return font_recipe.build(test_setup(), packs or {}, texts, language, options())

    def test_the_file_holds_seven_faces_of_the_extra_characters_and_the_covered_languages(self):
        text = covered_text(30)
        data, result = self.build([text], {"zh": [text[:10]], "fr": ["Retour"]})
        faces, languages = glyph_pack.read_extra_font(data)
        self.assertEqual(["fr", "zh"], languages)
        self.assertEqual(glyph_pack.EXTRA_FACES, len(faces))
        expected = sorted(ord(character) for character in text)
        for face in faces:
            self.assertEqual(expected, sorted(face))
        self.assertEqual(expected, result.characters)

    def test_identical_inputs_build_identical_bytes_whatever_their_order(self):
        text = covered_text(40)
        first, _ = self.build([text, "Héllo"], {"zh": [text[:5]], "fr": ["Retour"]})
        second, _ = self.build(["Héllo", text[::-1]], {"fr": ["Retour"], "zh": [text[:5]]})
        self.assertEqual(first, second)

    def test_the_style_changes_the_glyphs(self):
        text = covered_text(40)
        chinese, _ = self.build([text], language="zh")
        japanese, _ = self.build([text], language="ja")
        self.assertNotEqual(chinese, japanese)

    def test_faces_whose_only_or_last_glyph_is_empty_round_trip(self):
        space = font_build.empty_glyph(0x3000, 16)
        inked = font_build.render_glyph(font_build.load_font(FONTS / "inter" / "Inter-Regular.ttf", 16), ord("A"))
        faces = [glyph_pack.pack_glyphs([space]), glyph_pack.pack_glyphs([inked, space])]
        faces += [None] * (glyph_pack.EXTRA_FACES - len(faces))
        read, languages = glyph_pack.read_extra_font(glyph_pack.pack_extra_font(faces, ["zh"]))
        self.assertEqual({0x3000: (0, 0, 0, 0, 16, b"")}, read[0])
        self.assertEqual([ord("A"), 0x3000], sorted(read[1]))
        self.assertTrue(read[1][ord("A")][5])
        self.assertEqual((0, 0, 0, 0, 16, b""), read[1][0x3000])
        self.assertEqual([None] * (glyph_pack.EXTRA_FACES - 2), read[2:])
        self.assertEqual(["zh"], languages)

    def test_nothing_is_built_when_the_built_in_fonts_draw_everything(self):
        data, result = self.build(["Plain Latin text, Привет"])
        self.assertIsNone(data)
        self.assertEqual([], result.characters)

    def test_a_character_some_face_drops_is_reported_and_left_out_of_every_face(self):
        setup = test_setup()
        setup.recipe["faces"][5]["max_bitmap"] = 100
        text = covered_text(30)
        data, result = font_recipe.build(setup, {"zh": [text]}, [], "zh", options())
        faces, languages = glyph_pack.read_extra_font(data)
        self.assertTrue(result.unrenderable)
        self.assertEqual([], languages)
        for face in faces:
            self.assertEqual(result.characters, sorted(face))
        self.assertFalse(set(result.unrenderable) & set(result.characters))


class AcquisitionTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR"))
        self.addCleanup(directory.cleanup)
        self.cache = Path(directory.name) / "cache"
        self.source = subset_source("sc", "regular")
        self.fetched = []

    def fetch(self, data):
        def opener(url):
            self.fetched.append(url)
            return contextlib.closing(io.BytesIO(data))
        return opener

    def test_a_font_is_downloaded_once_verified_and_then_read_from_the_cache(self):
        data = (NOTO / "sc-Regular.otf").read_bytes()
        opener = self.fetch(data)
        path = font_recipe.font_file(self.source, font_recipe.Options(cache=self.cache, fetch=opener))
        self.assertEqual(data, path.read_bytes())
        font_recipe.font_file(self.source, font_recipe.Options(cache=self.cache, fetch=opener))
        self.assertEqual([self.source["url"]], self.fetched)
        self.assertEqual(["sc-Regular.otf"], sorted(item.name for item in self.cache.iterdir()))

    def test_a_download_that_does_not_match_the_pin_is_refused_and_not_kept(self):
        for data in (b"wrong bytes", (NOTO / "sc-Regular.otf").read_bytes() + b"tail"):
            with self.assertRaises(font_recipe.RecipeError):
                font_recipe.font_file(self.source, font_recipe.Options(cache=self.cache, fetch=self.fetch(data)))
            self.assertEqual([], list(self.cache.iterdir()))

    def test_a_damaged_cache_entry_is_downloaded_again(self):
        self.cache.mkdir()
        (self.cache / "sc-Regular.otf").write_bytes(b"damaged")
        data = (NOTO / "sc-Regular.otf").read_bytes()
        font_recipe.font_file(self.source, font_recipe.Options(cache=self.cache, fetch=self.fetch(data)))
        self.assertEqual(data, (self.cache / "sc-Regular.otf").read_bytes())

    def test_offline_the_error_names_the_file_and_the_fonts_dir_option(self):
        def offline(url):
            raise urllib.error.URLError("no network")
        with self.assertRaises(font_recipe.RecipeError) as raised:
            font_recipe.font_file(self.source, font_recipe.Options(cache=self.cache, fetch=offline))
        message = str(raised.exception)
        for part in ("sc-Regular.otf", "no network", str(self.cache), self.source["url"]):
            self.assertIn(part, message)

    def test_a_fonts_folder_must_hold_the_pinned_file(self):
        self.assertEqual(NOTO / "sc-Regular.otf", font_recipe.font_file(self.source, options()))
        wrong = {**self.source, "sha256": "0" * 64}
        with self.assertRaises(font_recipe.RecipeError):
            font_recipe.font_file(wrong, options())

    def test_the_cache_is_the_platforms_per_user_cache(self):
        home = Path("/home/user")
        name = font_recipe.IDENTITY.name
        self.assertEqual(home / "Library/Caches" / name / "fonts", font_recipe.cache_directory("darwin", {}, home))
        self.assertEqual(Path("/x/cache") / name / "fonts",
                         font_recipe.cache_directory("linux", {"XDG_CACHE_HOME": "/x/cache"}, home))
        self.assertEqual(home / ".cache" / name / "fonts", font_recipe.cache_directory("linux", {}, home))
        self.assertEqual(Path("C:/Users/u/AppData/Local") / name / "fonts",
                         font_recipe.cache_directory("win32", {"LOCALAPPDATA": "C:/Users/u/AppData/Local"}, home))


class InstallTest(unittest.TestCase):
    def setUp(self):
        self.volume = Volume(self)
        self.other = self.volume.internal / font_recipe.DATA_FOLDER / "settings.bin"
        self.other.write_bytes(b"keep")
        self.path = self.volume.fonts / "extra.font"

    def listing(self):
        return sorted(str(path.relative_to(self.volume.internal)) for path in self.volume.internal.rglob("*"))

    def test_the_file_is_written_whole_and_nothing_else_changes(self):
        before = self.listing()
        self.assertEqual("written", font_recipe.install(self.volume.internal, b"font bytes"))
        self.assertEqual(b"font bytes", self.path.read_bytes())
        self.assertEqual(sorted(before + [str(self.path.parent.relative_to(self.volume.internal)),
                                          str(self.path.relative_to(self.volume.internal))]), self.listing())
        self.assertEqual(b"keep", self.other.read_bytes())

    def test_unchanged_bytes_are_not_rewritten(self):
        font_recipe.install(self.volume.internal, b"font bytes")
        with mock.patch.object(font_recipe, "write_stage") as write:
            self.assertEqual("unchanged", font_recipe.install(self.volume.internal, b"font bytes"))
        write.assert_not_called()
        self.assertEqual("written", font_recipe.install(self.volume.internal, b"other bytes"))
        self.assertEqual(b"other bytes", self.path.read_bytes())

    def test_a_stale_file_is_removed_when_nothing_is_needed(self):
        self.assertEqual("absent", font_recipe.install(self.volume.internal, None))
        font_recipe.install(self.volume.internal, b"font bytes")
        self.assertEqual("would remove", font_recipe.install(self.volume.internal, None, dry_run=True))
        self.assertTrue(self.path.exists())
        self.assertEqual("removed", font_recipe.install(self.volume.internal, None))
        self.assertFalse(self.path.exists())
        self.assertEqual(b"keep", self.other.read_bytes())

    def test_a_dry_run_writes_nothing(self):
        self.assertEqual("would write", font_recipe.install(self.volume.internal, b"font", dry_run=True))
        self.assertFalse(self.volume.fonts.exists())

    def test_a_failed_write_keeps_the_old_file_and_leaves_no_stage(self):
        font_recipe.install(self.volume.internal, b"old")
        with mock.patch.object(font_recipe.os, "replace", side_effect=OSError("disk gone")):
            with self.assertRaises(OSError):
                font_recipe.install(self.volume.internal, b"new")
        self.assertEqual(b"old", self.path.read_bytes())
        self.assertEqual(["extra.font"], sorted(item.name for item in self.volume.fonts.iterdir()))

    def test_a_leftover_stage_is_replaced(self):
        self.volume.fonts.mkdir()
        (self.volume.fonts / "extra.font.part").write_bytes(b"partial")
        font_recipe.install(self.volume.internal, b"font")
        self.assertEqual(["extra.font"], sorted(item.name for item in self.volume.fonts.iterdir()))

    def test_free_space_is_checked_before_writing(self):
        usage = shutil.disk_usage(self.volume.internal)._replace(free=1000)
        with mock.patch.object(font_recipe.shutil, "disk_usage", return_value=usage):
            with self.assertRaises(font_recipe.RecipeError):
                font_recipe.install(self.volume.internal, b"font")
        self.assertFalse(self.path.exists())

    def test_a_volume_without_the_data_folder_or_with_a_linked_fonts_folder_is_refused(self):
        bare = self.volume.internal.parent / "bare"
        bare.mkdir()
        with self.assertRaises(font_recipe.RecipeError):
            font_recipe.install(bare, b"font")
        elsewhere = self.volume.internal.parent / "elsewhere"
        elsewhere.mkdir()
        self.volume.fonts.symlink_to(elsewhere, target_is_directory=True)
        with self.assertRaises(font_recipe.RecipeError):
            font_recipe.install(self.volume.internal, b"font")
        self.assertEqual([], list(elsewhere.iterdir()))


class RebuildTest(unittest.TestCase):
    def rebuild(self, volume, extras=None, dry_run=False):
        return font_recipe.rebuild(volume.volumes, extras, font_recipe.Options(NOTO, dry_run), test_setup())

    def test_a_rebuild_follows_the_saved_language_and_reports_what_it_did(self):
        volume = Volume(self)
        volume.settings("ja")
        volume.add_pack("ja", covered_text(2, 40), covered_text(6))
        volume.add_library("Track " + covered_text(4, 100) + " 🎵")
        report = self.rebuild(volume)
        self.assertEqual("written", report.action)
        self.assertEqual("jp", report.style)
        self.assertEqual(["ja"], report.languages)
        self.assertEqual([ord("🎵")], report.unrenderable)
        self.assertEqual(report.size, (volume.fonts / "extra.font").stat().st_size)
        summary = report.as_dict()
        self.assertEqual("🎵", summary["unrenderable"])
        self.assertIn("can't be drawn", report.summary())
        self.assertEqual("unchanged", self.rebuild(volume).action)

    def test_a_latin_only_player_gets_no_file_and_loses_a_stale_one(self):
        volume = Volume(self)
        volume.add_library("Plain Song")
        self.assertEqual("absent", self.rebuild(volume).action)
        volume.fonts.mkdir()
        (volume.fonts / "extra.font").write_bytes(b"stale")
        report = self.rebuild(volume)
        self.assertEqual("removed", report.action)
        self.assertEqual("none needed, the old file was removed", report.summary())

    def test_the_cli_builds_into_a_volume_and_into_a_file(self):
        volume = Volume(self)
        volume.add_library(covered_text(5))
        recipe = volume.internal.parent / "recipe.json"
        recipe.write_text(json.dumps(test_recipe()), encoding="utf-8")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = font_recipe.main(["build", "--internal", str(volume.internal), "--music", str(volume.card),
                                       "--recipe", str(recipe), "--fonts-dir", str(NOTO)])
        self.assertEqual(0, status)
        self.assertEqual("written", json.loads(output.getvalue())["action"])
        target = volume.internal.parent / "out.font"
        text = volume.internal.parent / "text.txt"
        text.write_text(covered_text(5), encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()):
            status = font_recipe.main(["render", "--output", str(target), "--text", str(text),
                                       "--library", str(volume.card / font_recipe.DATA_FOLDER / "library.db"),
                                       "--recipe", str(recipe), "--fonts-dir", str(NOTO)])
        self.assertEqual(0, status)
        self.assertEqual((volume.fonts / "extra.font").read_bytes(), target.read_bytes())

    def test_the_cli_reports_errors_without_a_traceback(self):
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            status = font_recipe.main(["build", "--internal", "/nonexistent-volume"])
        self.assertEqual(1, status)
        self.assertIn("font_recipe:", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
