import json
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

    def test_a_backslash_n_in_the_file_is_a_line_break_in_the_text(self):
        catalogue = self.read(english_file() + "STR_TWO = one\\ntwo\n")
        self.assertEqual(("STR_TWO", "one\ntwo"), catalogue[3])
        pack = lang_pack.build_pack("en", catalogue, {})
        self.assertIn(b"one\ntwo\0", pack)
        lang_pack.check_glyphs("en", catalogue, DRAWABLE)
        self.assertIn('[STR_TWO] = "one\\ntwo",', lang_pack.english_source(catalogue))
        self.assertEqual("[óné\ntwó!!]", lang_pack.pseudo_text("one\ntwo"))

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
    def test_version_one_offsets_and_validation_support_an_eighteen_kibibyte_window(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {"STR_HELLO": "x" * 17000})
        self.assertGreater(len(pack), 16384)
        self.assertLessEqual(len(pack), 18432)
        self.assertEqual(1, lang_pack.HEADER.unpack_from(pack)[1])
        self.assertEqual("en", lang_pack.validate_pack(pack, CATALOGUE, DRAWABLE)["code"])

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
            self.pack({"ja.txt": "STR_HELLO = 🦊\n"}, "ja")
        self.assertIn("U+1F98A", str(refused.exception))

    def test_a_missing_translation_file_is_an_error(self):
        with self.assertRaises(FileNotFoundError):
            self.pack({}, "fr")


class GlyphCoverageTest(unittest.TestCase):
    def test_every_text_size_draws_ascii_and_latin_1(self):
        drawable = lang_pack.drawable_characters()
        self.assertTrue(set(range(0x20, 0x7F)) <= drawable)
        self.assertTrue(set(range(0xA0, 0x100)) <= drawable)
        self.assertTrue({0x2026, 0x2605, 0x2713, 0x203A} <= drawable)

    def test_cyrillic_is_drawable_at_every_text_size(self):
        drawable = lang_pack.drawable_characters()
        self.assertTrue(set(range(0x400, 0x460)) <= drawable)
        lang_pack.check_glyphs("ru", [("STR_HELLO", "Привет")], drawable)

    def test_fallback_faces_contribute_only_when_available_at_every_size(self):
        fonts = {"character_ranges": [], "fonts": [
            {"pack_face": "a", "fallback_faces": ["a_cjk"]},
            {"pack_face": "b", "fallback_faces": ["b_cjk"]},
        ]}
        packs = {"faces": [
            {"name": "a", "ranges": [[65, 65]]},
            {"name": "b", "ranges": [[65, 65]]},
            {"name": "a_cjk", "ranges": [[0x4E2D, 0x4E2D]]},
            {"name": "b_cjk", "ranges": [[0x4E2D, 0x4E2D]]},
        ]}
        self.assertEqual({65, 0x4E2D}, lang_pack.drawable_from_config(fonts, packs))
        packs["faces"].pop()
        self.assertEqual({65}, lang_pack.drawable_from_config(fonts, packs))

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


def rechecked(pack):
    changed = bytearray(pack)
    checksum = lang_pack.fnv(changed[lang_pack.CHECKSUM_END:],
                             lang_pack.fnv(changed[:lang_pack.CHECKSUM_OFFSET]))
    struct.pack_into("<I", changed, lang_pack.CHECKSUM_OFFSET, checksum)
    return bytes(changed)


def header_changed(pack, field, value):
    values = list(lang_pack.HEADER.unpack_from(pack))
    values[field] = value
    changed = bytearray(pack)
    changed[:lang_pack.HEADER.size] = lang_pack.HEADER.pack(*values)
    return rechecked(changed)


class PackCodeValidationTest(unittest.TestCase):
    def test_a_checksummed_code_with_forbidden_characters_is_refused(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {})
        for code in (b"en$" + bytes(5), b"abcdef" + bytes(2), bytes(8)):
            with self.subTest(code=code), self.assertRaises(lang_pack.PackError):
                lang_pack.pack_code(header_changed(pack, 6, code))

    def test_hidden_metadata_after_the_first_nul_is_refused(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {})
        with self.assertRaises(lang_pack.PackError):
            lang_pack.pack_code(header_changed(pack, 6, b"en\0x" + bytes(4)))
        with self.assertRaises(lang_pack.PackError):
            lang_pack.pack_code(header_changed(pack, 7, b"English\0x" + bytes(19)))


class FullPackValidationTest(unittest.TestCase):
    def validate(self, pack, catalogue=CATALOGUE, drawable=DRAWABLE):
        return lang_pack.validate_pack(pack, catalogue, drawable)

    def test_generated_packs_return_complete_metadata_and_texts(self):
        for code, translations in (("en", {}), ("de", {"STR_LANGUAGE_NAME": "Deutsch",
                                                        "STR_HELLO": "Grüße"})):
            pack = lang_pack.build_pack(code, CATALOGUE, translations)
            self.assertEqual({"code": code, "name": translations.get("STR_LANGUAGE_NAME", "English"),
                              "count": len(CATALOGUE), "size": len(pack),
                              "catalogue_hash": lang_pack.catalogue_hash(CATALOGUE),
                              "texts": {name: translations.get(name, english)
                                        for name, english in CATALOGUE}}, self.validate(pack))

    def test_format_version_exact_length_and_checksum_are_checked(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {})
        bad = [b"", pack[:55], pack[:-1], pack + b"\0", header_changed(pack, 0, b"NOPE"),
               header_changed(pack, 1, 2), header_changed(pack, 3, len(pack) - 1)]
        damaged = bytearray(pack)
        damaged[-2] ^= 1
        bad.append(bytes(damaged))
        for value in bad:
            with self.subTest(length=len(value)), self.assertRaises(lang_pack.PackError):
                self.validate(value)

    def test_a_checksummed_pack_larger_than_the_player_capacity_is_refused(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {})
        oversized = pack + bytes(lang_pack.PACK_LIMIT + 1 - len(pack))
        oversized = header_changed(oversized, 3, len(oversized))
        with self.assertRaises(lang_pack.PackError):
            self.validate(oversized)

    def test_count_and_catalogue_hash_must_match_the_target_catalogue(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {})
        for bad in (header_changed(pack, 2, len(CATALOGUE) - 1),
                    header_changed(pack, 4, lang_pack.catalogue_hash(CATALOGUE) ^ 1)):
            with self.assertRaises(lang_pack.PackError):
                self.validate(bad)
        renamed = [CATALOGUE[0], ("STR_OTHER", "Hello"), CATALOGUE[2]]
        with self.assertRaises(lang_pack.PackError):
            self.validate(pack, renamed)

    def test_metadata_needs_utf8_nul_termination_and_zero_padding(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {})
        for field, value in ((6, b"a" * lang_pack.CODE_BYTES),
                             (6, b"\xff" + bytes(lang_pack.CODE_BYTES - 1)),
                             (7, b"N" * lang_pack.NAME_BYTES),
                             (7, b"\xff" + bytes(lang_pack.NAME_BYTES - 1)),
                             (7, bytes(lang_pack.NAME_BYTES)),
                             (7, b"English\0X" + bytes(19))):
            with self.subTest(field=field, value=value), self.assertRaises(lang_pack.PackError):
                self.validate(header_changed(pack, field, value))

    def test_header_name_must_equal_the_catalogue_language_name(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {})
        with self.assertRaises(lang_pack.PackError):
            self.validate(header_changed(pack, 7, b"Other" + bytes(23)))

    def test_offsets_are_body_relative_and_must_start_a_terminated_string(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {})
        base = lang_pack.HEADER.size + 2 * len(CATALOGUE)
        for offset in (len(pack) - base, 0xFFFF, 1):
            changed = bytearray(pack)
            struct.pack_into("<H", changed, lang_pack.HEADER.size + 2, offset)
            with self.subTest(offset=offset), self.assertRaises(lang_pack.PackError):
                self.validate(rechecked(changed))
        changed = bytearray(pack)
        changed[-1] = ord("x")
        with self.assertRaises(lang_pack.PackError):
            self.validate(rechecked(changed))

    def test_shared_string_offsets_are_valid(self):
        catalogue = [("STR_LANGUAGE_NAME", "English"), ("STR_A", "Same"), ("STR_B", "Same")]
        pack = bytearray(lang_pack.build_pack("en", catalogue, {}))
        first = struct.unpack_from("<H", pack, lang_pack.HEADER.size + 2)[0]
        struct.pack_into("<H", pack, lang_pack.HEADER.size + 4, first)
        checked = self.validate(rechecked(pack), catalogue)
        self.assertEqual("Same", checked["texts"]["STR_B"])

    def test_invalid_utf8_and_empty_body_strings_are_refused(self):
        pack = lang_pack.build_pack("en", CATALOGUE, {})
        base = lang_pack.HEADER.size + 2 * len(CATALOGUE)
        hello = struct.unpack_from("<H", pack, lang_pack.HEADER.size + 2)[0]
        for value in (0, 0xFF):
            changed = bytearray(pack)
            changed[base + hello] = value
            with self.subTest(value=value), self.assertRaises(lang_pack.PackError):
                self.validate(rechecked(changed))

    def test_translations_keep_the_existing_placeholder_set_semantics(self):
        good = lang_pack.build_pack("xx", CATALOGUE, {"STR_COUNT": "{1} / {0}, {0}"})
        self.assertEqual("{1} / {0}, {0}", self.validate(good)["texts"]["STR_COUNT"])
        for text in ("Songs", "{0} only", "{0} {1} {2}"):
            with self.subTest(text=text), self.assertRaises(lang_pack.PackError):
                self.validate(lang_pack.build_pack("xx", CATALOGUE, {"STR_COUNT": text}))

    def test_unsupported_glyphs_in_any_string_are_refused(self):
        with self.assertRaises(lang_pack.PackError):
            self.validate(lang_pack.build_pack("xx", CATALOGUE, {"STR_HELLO": "Ж"}))
        pack = lang_pack.build_pack("xx", CATALOGUE, {"STR_HELLO": "one\ntwo"})
        self.assertEqual("one\ntwo", self.validate(pack)["texts"]["STR_HELLO"])

    def test_real_english_and_pseudo_packs_validate(self):
        catalogue = lang_pack.read_catalogue()
        drawable = lang_pack.drawable_characters()
        for code in (lang_pack.ENGLISH, lang_pack.PSEUDO):
            pack = lang_pack.language_pack(code)
            result = lang_pack.validate_pack(pack, catalogue, drawable)
            self.assertEqual(code, result["code"])
            self.assertEqual(len(catalogue), len(result["texts"]))


class CatalogueExportTest(unittest.TestCase):
    def test_catalogue_text_uses_the_same_rules_without_a_source_file(self):
        self.assertEqual(CATALOGUE, lang_pack.read_catalogue_text(english_file(), "commit:en.txt"))
        with self.assertRaises(lang_pack.PackError) as refused:
            lang_pack.read_catalogue_text("STR_A = A\n", "commit:en.txt")
        self.assertIn("commit:en.txt", str(refused.exception))

    def test_font_configuration_can_be_resolved_without_a_source_directory(self):
        fonts = json.loads((lang_pack.FONTS / "fonts.json").read_text())
        packs = json.loads((lang_pack.FONTS / "glyph_pack.json").read_text())
        self.assertEqual(lang_pack.drawable_characters(), lang_pack.drawable_from_config(fonts, packs))

    def test_catalogue_info_is_json_ready_with_ordered_entries_and_sorted_glyphs(self):
        info = lang_pack.catalogue_info(CATALOGUE, DRAWABLE)
        self.assertEqual({"version": 1, "magic": "DSLP", "pack_limit": 18432,
                          "code_limit": 5, "code_bytes": 8, "name_bytes": 28,
                          "name_id": "STR_LANGUAGE_NAME",
                          "catalogue_hash": lang_pack.catalogue_hash(CATALOGUE),
                          "entries": [{"id": name, "english": text} for name, text in CATALOGUE],
                          "glyphs": sorted(DRAWABLE)}, info)
        self.assertEqual(info, json.loads(json.dumps(info)))

    def test_browser_export_is_deterministic_and_checkable(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "catalogue.js"
            arguments = ["--export-catalogue", str(destination)]
            self.assertEqual(0, lang_pack.main(arguments))
            first = destination.read_text(encoding="utf-8")
            prefix = "globalThis.LANGUAGE_CATALOGUE = "
            self.assertTrue(first.startswith(prefix))
            self.assertEqual(lang_pack.catalogue_info(lang_pack.read_catalogue(),
                                                      lang_pack.drawable_characters()),
                             json.loads(first[len(prefix):].rstrip(";\n")))
            self.assertEqual(0, lang_pack.main(arguments))
            self.assertEqual(first, destination.read_text(encoding="utf-8"))
            self.assertEqual(0, lang_pack.main(arguments + ["--check"]))
            destination.write_text("stale\n")
            self.assertEqual(1, lang_pack.main(arguments + ["--check"]))

    def test_the_committed_browser_export_matches_the_catalogue_and_fonts(self):
        destination = ROOT.parent / "site/language-catalog.js"
        self.assertEqual(lang_pack.browser_source(lang_pack.read_catalogue(),
                                                  lang_pack.drawable_characters()),
                         destination.read_text(encoding="utf-8"),
                         "Regenerate with python3 tools/firmware.py regen from the repository root")


class NativeNameValidationTest(unittest.TestCase):
    def test_native_names_cannot_contain_line_breaks_or_nonprintable_characters(self):
        for character in ("\n", "\r", "\t", "\0", "\x1b", "\x7f", "\x85", "\u200b", "\u2028"):
            with self.subTest(character=repr(character)), self.assertRaises(lang_pack.PackError):
                lang_pack.build_pack("xx", CATALOGUE, {"STR_LANGUAGE_NAME": "A" + character + "B"})

    def test_checksummed_native_names_with_controls_are_refused(self):
        good = lang_pack.build_pack("en", CATALOGUE, {})
        base = lang_pack.HEADER.size + 2 * len(CATALOGUE)
        for name in ("E\nglish", "E\rglish", "E\tglish", "E\x1bglish", "Engli\x7fh", "En\x85ish", "En\u2028sh"):
            encoded = name.encode("utf-8")
            self.assertEqual(7, len(encoded))
            changed = bytearray(header_changed(good, 7, encoded.ljust(lang_pack.NAME_BYTES, b"\0")))
            changed[base:base + len(encoded)] = encoded
            with self.subTest(name=repr(name)), self.assertRaises(lang_pack.PackError):
                lang_pack.validate_pack(rechecked(changed), CATALOGUE, DRAWABLE | {ord(c) for c in name})

    def test_direct_pack_building_refuses_embedded_nuls_in_every_translation(self):
        for key, text in (("STR_HELLO", "A\0B"), ("STR_COUNT", "{0}\0{1}"),
                          ("STR_LANGUAGE_NAME", "English\0")):
            with self.subTest(key=key), self.assertRaises(lang_pack.PackError):
                lang_pack.build_pack("xx", CATALOGUE, {key: text})

    def test_message_line_breaks_and_printable_accented_names_still_work(self):
        pack = lang_pack.build_pack("fr", CATALOGUE, {"STR_LANGUAGE_NAME": "Français",
                                                      "STR_HELLO": "one\ntwo"})
        checked = lang_pack.validate_pack(pack, CATALOGUE, DRAWABLE)
        self.assertEqual("Français", checked["name"])
        self.assertEqual("one\ntwo", checked["texts"]["STR_HELLO"])


if __name__ == "__main__":
    unittest.main()
