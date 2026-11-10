import re
import struct
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
ROOT = TOOLS_DIRECTORY.parent
sys.path.insert(0, str(TOOLS_DIRECTORY))

import lang_pack

CATALOGUE = [("STR_LANGUAGE_NAME", "English"), ("STR_HELLO", "Hello"),
             ("STR_COUNT", "{0} of {1} songs")]
DRAWABLE = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))


def write_languages(directory, files):
    for name, text in files.items():
        (Path(directory) / name).write_text(text, encoding="utf-8")


def english_file(catalogue=CATALOGUE):
    return "# a note\n\n" + "".join(f"{name} = {text}\n" for name, text in catalogue)


def strings_of(pack):
    _, _, count, size, _, _, _, _ = lang_pack.HEADER.unpack_from(pack)
    base = lang_pack.HEADER.size + 2 * count
    offsets = struct.unpack_from(f"<{count}H", pack, lang_pack.HEADER.size)
    return [pack[base + offset:pack.index(b"\0", base + offset)].decode("utf-8")
            for offset in offsets]


class CatalogueTest(unittest.TestCase):
    def read(self, text):
        with tempfile.TemporaryDirectory() as directory:
            write_languages(directory, {"en.txt": text})
            return lang_pack.read_catalogue(directory)

    def test_names_and_texts_come_in_file_order(self):
        self.assertEqual(CATALOGUE, self.read(english_file()))

    def test_notes_and_blank_lines_are_skipped(self):
        self.assertEqual(3, len(self.read("# one\n\n  # two\n" + english_file())))

    def test_spaces_around_the_text_are_dropped(self):
        catalogue = self.read("STR_LANGUAGE_NAME =   English  \nSTR_A=  b c \n")
        self.assertEqual(("STR_A", "b c"), catalogue[1])

    def test_a_line_that_is_not_a_string_is_refused(self):
        for line in ("Hello", "str_lower = x", "NAME = x", "STR_A: x"):
            with self.assertRaises(lang_pack.PackError):
                self.read(english_file() + line + "\n")

    def test_a_name_used_twice_is_refused(self):
        with self.assertRaises(lang_pack.PackError):
            self.read(english_file() + "STR_HELLO = again\n")

    def test_an_empty_english_text_is_refused(self):
        with self.assertRaises(lang_pack.PackError):
            self.read(english_file() + "STR_EMPTY =\n")

    def test_the_language_name_is_required(self):
        with self.assertRaises(lang_pack.PackError):
            self.read("STR_HELLO = Hello\n")

    def test_placeholders_must_be_numbered_from_zero(self):
        with self.assertRaises(lang_pack.PackError):
            self.read(english_file() + "STR_GAP = {1} only\n")
        self.assertEqual(4, len(self.read(english_file() + "STR_SWAPPED = {1} then {0}\n")))

    def test_the_hash_follows_the_names_and_their_order_not_the_texts(self):
        renamed = [("STR_LANGUAGE_NAME", "English"), ("STR_HOLA", "Hello"), CATALOGUE[2]]
        reworded = [("STR_LANGUAGE_NAME", "Deutsch"), ("STR_HELLO", "Hallo"), CATALOGUE[2]]
        swapped = [CATALOGUE[0], CATALOGUE[2], CATALOGUE[1]]
        value = lang_pack.catalogue_hash(CATALOGUE)
        self.assertEqual(value, lang_pack.catalogue_hash(reworded))
        self.assertNotEqual(value, lang_pack.catalogue_hash(renamed))
        self.assertNotEqual(value, lang_pack.catalogue_hash(swapped))


class PackTest(unittest.TestCase):
    def test_the_header_names_the_format_the_catalogue_and_the_language(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {})
        magic, version, count, size, catalogue, _, code, name = lang_pack.HEADER.unpack_from(pack)
        self.assertEqual((b"DSLP", 1, 3, len(pack)), (magic, version, count, size))
        self.assertEqual(lang_pack.catalogue_hash(CATALOGUE), catalogue)
        self.assertEqual(b"en" + bytes(6), code)
        self.assertEqual(b"English", name.rstrip(b"\0"))
        self.assertEqual(56, lang_pack.HEADER.size)

    def test_the_strings_follow_the_offset_table_in_catalogue_order(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {})
        self.assertEqual(["English", "Hello", "{0} of {1} songs"], strings_of(pack))
        self.assertEqual(0, pack[-1])

    def test_a_translation_replaces_the_english_and_a_missing_one_keeps_it(self):
        pack = lang_pack.build_pack("de", CATALOGUE, {"STR_LANGUAGE_NAME": "Deutsch",
                                                      "STR_HELLO": "Hallo", "STR_COUNT": ""})
        self.assertEqual(["Deutsch", "Hallo", "{0} of {1} songs"], strings_of(pack))
        self.assertEqual("de", lang_pack.pack_code(pack))

    def test_the_checksum_covers_everything_but_itself(self):
        pack = bytearray(lang_pack.build_pack("en", CATALOGUE, {}))
        self.assertEqual("en", lang_pack.pack_code(bytes(pack)))
        for offset in (5, 20, 30, 60, len(pack) - 2):
            damaged = bytearray(pack)
            damaged[offset] ^= 1
            with self.assertRaises(lang_pack.PackError):
                lang_pack.pack_code(bytes(damaged))

    def test_a_cut_or_padded_pack_is_refused(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {})
        for wrong in (pack[:-1], pack + b"\0", pack[:20], b""):
            with self.assertRaises(lang_pack.PackError):
                lang_pack.pack_code(wrong)

    def test_a_code_is_one_to_five_letters_digits_or_dashes(self):
        for code in ("pt-BR", "x", "zh2"):
            self.assertEqual(code, lang_pack.pack_code(lang_pack.build_pack(code, CATALOGUE, {})))
        for code in ("", "toolong", "a b", "en.", "é"):
            with self.assertRaises(lang_pack.PackError):
                lang_pack.build_pack(code, CATALOGUE, {})

    def test_a_language_name_that_does_not_fit_the_header_is_refused(self):
        with self.assertRaises(lang_pack.PackError):
            lang_pack.build_pack("xx", CATALOGUE, {"STR_LANGUAGE_NAME": "N" * lang_pack.NAME_BYTES})

    def test_a_pack_over_the_players_memory_is_refused(self):
        with self.assertRaises(lang_pack.PackError):
            lang_pack.build_pack("xx", CATALOGUE, {"STR_HELLO": "x" * lang_pack.PACK_LIMIT})

    def test_the_limit_is_the_memory_the_player_gives_a_pack(self):
        header = (ROOT / "src" / "core" / "include" / "core" / "i18n.h").read_text()
        constants = dict(re.findall(r"(I18N_\w+) = (\d+)", header))
        self.assertEqual(lang_pack.PACK_LIMIT, int(constants["I18N_PACK_CAPACITY"]))
        self.assertEqual(lang_pack.HEADER.size, int(constants["I18N_HEADER_BYTES"]))
        self.assertEqual(lang_pack.CODE_BYTES, int(constants["I18N_CODE_BYTES"]))
        self.assertEqual(lang_pack.NAME_BYTES, int(constants["I18N_NAME_BYTES"]))
        self.assertEqual(lang_pack.VERSION, int(constants["I18N_VERSION"]))

    def test_a_settings_file_can_hold_every_code(self):
        header = (ROOT / "src" / "settings" / "include" / "settings" / "settings.h").read_text()
        length = int(re.search(r"SETTINGS_LANGUAGE_LENGTH = (\d+)", header).group(1))
        self.assertEqual(lang_pack.CODE_LIMIT, length)


class TranslationTest(unittest.TestCase):
    def pack(self, files, code):
        with tempfile.TemporaryDirectory() as directory:
            write_languages(directory, {"en.txt": english_file(), **files})
            return lang_pack.language_pack(code, directory)

    def test_a_translation_file_may_list_its_strings_in_any_order(self):
        pack = self.pack({"de.txt": "STR_COUNT = {1} Lieder, {0}\nSTR_LANGUAGE_NAME = Deutsch\n"}, "de")
        self.assertEqual(["Deutsch", "Hello", "{1} Lieder, {0}"], strings_of(pack))

    def test_an_unknown_name_is_refused(self):
        with self.assertRaises(lang_pack.PackError):
            self.pack({"de.txt": "STR_NOPE = x\n"}, "de")

    def test_a_translation_must_keep_the_placeholders(self):
        for text in ("{0} Lieder", "{0} von {1} von {2}", "Lieder"):
            with self.assertRaises(lang_pack.PackError):
                self.pack({"de.txt": f"STR_COUNT = {text}\n"}, "de")

    def test_a_character_some_text_size_cannot_draw_is_refused(self):
        with self.assertRaises(lang_pack.PackError) as refused:
            self.pack({"ja.txt": "STR_HELLO = こんにちは\n"}, "ja")
        self.assertIn("U+3053", str(refused.exception))

    def test_a_missing_translation_file_is_an_error(self):
        with self.assertRaises(FileNotFoundError):
            self.pack({}, "fr")


class GlyphCoverageTest(unittest.TestCase):
    def test_every_text_size_draws_ascii_and_latin_1(self):
        drawable = lang_pack.drawable_characters()
        self.assertTrue(set(range(0x20, 0x7F)) <= drawable)
        self.assertTrue(set(range(0xA0, 0x100)) <= drawable)
        self.assertTrue({0x2026, 0x2605, 0x2713, 0x203A} <= drawable)

    def test_cyrillic_is_missing_from_the_small_font_only(self):
        self.assertNotIn(0x416, lang_pack.drawable_characters())
        with self.assertRaises(lang_pack.PackError):
            lang_pack.check_glyphs("ru", [("STR_HELLO", "Привет")], lang_pack.drawable_characters())

    def test_text_in_the_drawable_set_passes(self):
        lang_pack.check_glyphs("fr", [("STR_HELLO", "Déjà vu… ★")], DRAWABLE | {0x2026, 0x2605})


class PseudoTest(unittest.TestCase):
    def test_every_string_is_bracketed_accented_and_longer(self):
        self.assertEqual("[Séttíngs!!!]", lang_pack.pseudo_text("Settings"))
        self.assertEqual("[Ón!]", lang_pack.pseudo_text("On"))

    def test_placeholders_survive_untouched(self):
        self.assertEqual("[{0} óf {1} sóngs!!!]", lang_pack.pseudo_text("{0} of {1} songs"))
        self.assertEqual("[{0}%]", lang_pack.pseudo_text("{0}%"))

    def test_a_lone_brace_is_kept(self):
        self.assertEqual("[á { b!]", lang_pack.pseudo_text("a { b"))

    def test_the_test_language_names_itself(self):
        translations = lang_pack.pseudo_translations(CATALOGUE)
        self.assertEqual("[Pseudo]", translations["STR_LANGUAGE_NAME"])
        self.assertEqual("qps", lang_pack.PSEUDO)

    def test_the_test_language_of_the_real_catalogue_fits_the_player(self):
        pack = lang_pack.language_pack(lang_pack.PSEUDO)
        self.assertEqual("qps", lang_pack.pack_code(pack))
        self.assertLessEqual(len(pack), lang_pack.PACK_LIMIT)


class RealCatalogueTest(unittest.TestCase):
    def test_the_english_pack_builds_and_holds_the_catalogue(self):
        catalogue = lang_pack.read_catalogue()
        pack = lang_pack.language_pack("en")
        self.assertEqual([text for _, text in catalogue], strings_of(pack))
        self.assertEqual("en", lang_pack.pack_code(pack))

    def test_no_english_text_holds_a_product_name(self):
        import identity
        for name, text in lang_pack.read_catalogue():
            self.assertNotIn(identity.IDENTITY.short_name, text, name)

    def test_the_committed_headers_match_the_catalogue(self):
        catalogue = lang_pack.read_catalogue()
        header = (ROOT / "src" / "core" / "include" / "core" / "strings.h").read_text()
        names = re.findall(r"^    (STR_\w+),$", header, re.MULTILINE)
        self.assertEqual([name for name, _ in catalogue], names)
        self.assertIn(f"0x{lang_pack.catalogue_hash(catalogue):08X}", header)


class GeneratedSourceTest(unittest.TestCase):
    def test_the_header_lists_the_ids_and_the_catalogue_hash(self):
        source = lang_pack.header_source(CATALOGUE)
        self.assertIn("    STR_HELLO,\n    STR_COUNT,\n    STRING_COUNT\n", source)
        self.assertIn(f"STRINGS_HASH UINT32_C(0x{lang_pack.catalogue_hash(CATALOGUE):08X})", source)

    def test_the_english_table_escapes_quotes_and_backslashes(self):
        source = lang_pack.english_source([("STR_LANGUAGE_NAME", "English"), ("STR_Q", 'a "b" \\ c')])
        self.assertIn('[STR_Q] = "a \\"b\\" \\\\ c",', source)

    def test_the_command_writes_a_pack_and_checks_the_headers(self):
        with tempfile.TemporaryDirectory() as directory:
            languages = Path(directory) / "lang"
            languages.mkdir()
            write_languages(languages, {"en.txt": english_file()})
            sources = Path(directory) / "core"
            output = Path(directory) / "out" / "en.pack"
            common = ["--languages", str(languages), "--sources", str(sources)]
            self.assertEqual(1, lang_pack.main(common + ["--check"]))
            self.assertEqual(0, lang_pack.main(common + ["--pack", "en", "--output", str(output)]))
            self.assertEqual(0, lang_pack.main(common + ["--check"]))
            self.assertEqual("en", lang_pack.pack_code(output.read_bytes()))
            write_languages(languages, {"en.txt": english_file() + "STR_NEW = New\n"})
            self.assertEqual(1, lang_pack.main(common + ["--check"]))


if __name__ == "__main__":
    unittest.main()
