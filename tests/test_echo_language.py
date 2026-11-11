import importlib.util
import json
import os
import re
import struct
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path[:0] = [str(Path(__file__).resolve().parent), str(Path(__file__).resolve().parents[1] / "tools")]
import echo_debug
import echo_debug_mcp
import echo_language
from test_echo_debug_loop import Clock, PlayerHost, script_report
from test_echo_debug import VolumeCase, SYSTEM_LINES, report_text, medium, player, usb_tree, FakeHost

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("language_fixture", ROOT / "firmware/tools/lang_pack.py")
PACK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACK)
BUILD = subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"], cwd=ROOT, text=True).strip()
CATALOGUE = PACK.read_catalogue_text(subprocess.check_output(
    ["git", "show", "HEAD:firmware/assets/lang/en.txt"], cwd=ROOT, text=True))


def pack(code="fr", name="Francais", **texts):
    return PACK.build_pack(code, CATALOGUE, {"STR_LANGUAGE_NAME": name, **texts})


def settings(code="en", version=13, flags=0, extension=None):
    data = bytearray(58 if version >= 15 else 42)
    data[:4] = b"DSST"
    data[4] = version
    data[5] = flags
    data[19:19 + len(code)] = code.encode("ascii")
    data[24] = 2
    field = 38
    if version >= 15:
        data[38:54] = extension or bytes([0x07, 24, 15, 0, 0x10, 0x32, 0x54, 0x76, 0x08]) + bytes(7)
        field = 54
    struct.pack_into("<I", data, field, PACK.fnv(data[:field]))
    return bytes(data)


class LanguageCommands(VolumeCase):
    def setUp(self):
        super().setUp()
        self.write_report(report_text([line.replace("1051212", BUILD) for line in SYSTEM_LINES]))
        (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(settings())
        self.lang = self.card / echo_debug.DATA_DIRECTORY / "lang"
        self.lang.mkdir()
        self.pack = Path(self.directory.name) / "fr.pack"
        self.pack.write_bytes(pack())

    def test_inspect_validates_against_explicit_clean_build_without_device(self):
        status, result = self.run_cli("language-inspect", str(self.pack), "--build", BUILD,
                                      host=FakeHost(usb_tree()))
        self.assertEqual(status, 0)
        self.assertEqual(result["pack"]["code"], "fr")
        self.assertEqual(result["firmware_build"], BUILD)

    def test_list_prefers_the_card_from_the_same_player(self):
        (self.lang / "fr.pack").write_bytes(pack())
        status, result = self.run_cli("language-list", host=self.host())
        self.assertEqual(status, 0)
        self.assertEqual(result["music_volume"], str(self.card))
        self.assertEqual([entry["code"] for entry in result["languages"]], ["en", "fr"])
        self.assertTrue(result["languages"][0]["selected"])

    def test_unknown_or_dirty_installed_build_refuses_mutation(self):
        for build in (BUILD + "+", "unknown", "f" * 40):
            with self.subTest(build=build):
                self.write_report(report_text([line.replace("1051212", build) for line in SYSTEM_LINES]))
                status, result = self.run_cli("language-install", str(self.pack), host=self.host())
                self.assertEqual(status, 1)
                self.assertIn(result["error"]["code"], ("language_build_unknown", "language_build_dirty"))
                self.assertEqual(list(self.lang.iterdir()), [])

    def test_a_music_override_cannot_escape_the_identified_player(self):
        status, result = self.run_cli("language-install", str(self.pack), "--music-volume",
                                      str(Path(self.directory.name)), host=self.host())
        self.assertEqual(status, 1)
        self.assertEqual(result["error"]["code"], "language_volume_refused")

    def test_builtin_english_cannot_be_replaced(self):
        self.pack.write_bytes(pack("en", "English"))
        status, result = self.run_cli("language-install", str(self.pack), host=self.host())
        self.assertEqual(status, 1)
        self.assertEqual(result["error"]["code"], "language_english_protected")


    def live_tool(self, selected=None, version=13, flags=0):
        self.sent = []
        def act():
            text = (self.debug / "run.txt").read_text()
            self.sent.append(text)
            if selected is not None:
                (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(settings(selected, version, flags))
            self.write_report(script_report(text, build=BUILD, screenshots=()))
        host = PlayerHost(self, act=act)
        clock = Clock()
        return echo_debug.Tool(host, sleep=clock.sleep, clock=clock.time), host

    def test_install_and_select_publishes_checked_pack_then_reconnects(self):
        tool, host = self.live_tool("fr")
        result = tool.language_install(self.pack, select=True)
        self.assertEqual((self.lang / "fr.pack").read_bytes(), pack())
        self.assertIsNone(result["backup_file"])
        self.assertEqual(result["selected"], "fr")
        self.assertEqual(host.ejected, [str(self.root), str(self.card)])
        self.assertEqual(self.sent, ["build " + BUILD + "\nset Language=Francais\nwait 250\nreconnect\n"])
        self.assertFalse((self.lang / "fr.pack.part").exists())

    def test_matching_volume_override_still_ejects_and_verifies_language_refresh(self):
        for select in (False, True):
            with self.subTest(select=select):
                tool, host = self.live_tool("fr" if select else None)
                tool.volume = self.root
                result = tool.language_install(self.pack, select=select)
                self.assertTrue(result["run"]["waited"])
                self.assertEqual(host.ejected, [str(self.root), str(self.card)])
                self.assertEqual(result["selected"], "fr" if select else "en")

    def test_macos_metadata_files_do_not_block_language_management(self):
        metadata = self.lang / "._fr.pack"
        metadata.write_bytes(b"AppleDouble metadata")
        (self.lang / "fr.pack").write_bytes(pack())
        tool, _ = self.live_tool("fr")
        self.assertEqual([item["code"] for item in tool.language_list()["languages"]], ["en", "fr"])
        self.assertEqual(tool.language_install(self.pack, select=True)["selected"], "fr")
        self.assertEqual(metadata.read_bytes(), b"AppleDouble metadata")

    def test_replacement_keeps_the_previous_pack_as_a_backup(self):
        old = pack(name="Ancien")
        (self.lang / "fr.pack").write_bytes(old)
        tool, _ = self.live_tool()
        result = tool.language_install(self.pack)
        self.assertEqual((self.lang / "fr.pack.bak").read_bytes(), old)
        self.assertEqual((self.lang / "fr.pack").read_bytes(), pack())
        self.assertEqual(result["backup_file"], str(self.lang / "fr.pack.bak"))
        self.assertFalse(any(item.name.endswith(".part") for item in self.lang.iterdir()))

    def test_selection_uses_the_current_pack_localized_setting_label(self):
        (self.lang / "fr.pack").write_bytes(pack(STR_LANGUAGE="Langue"))
        (self.lang / "de.pack").write_bytes(pack("de", "Deutsch"))
        (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(settings("fr"))
        tool, _ = self.live_tool("de")
        result = tool.language_select("de")
        self.assertEqual(result["selected"], "de")
        self.assertIn("set Langue=Deutsch\n", self.sent[0])

    def test_a_missing_saved_pack_uses_builtin_english_for_selection(self):
        (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(settings("es"))
        tool, _ = self.live_tool("en")
        result = tool.language_select("en")
        self.assertEqual(result["selected"], "en")
        self.assertIn("set Language=English\n", self.sent[0])

    def test_a_wrong_catalogue_is_refused_before_any_write(self):
        self.pack.write_bytes(PACK.build_pack("fr", CATALOGUE[:-1], {"STR_LANGUAGE_NAME": "Francais"}))
        tool, host = self.live_tool()
        with self.assertRaises(echo_debug.DebugError) as raised:
            tool.language_install(self.pack)
        self.assertEqual(raised.exception.code, "language_pack_invalid")
        self.assertEqual(list(self.lang.iterdir()), [])
        self.assertEqual(host.ejected, [])

    def test_symlink_language_directory_and_pack_are_refused(self):
        outside = Path(self.directory.name) / "outside"
        outside.mkdir()
        self.lang.rmdir()
        self.lang.symlink_to(outside, target_is_directory=True)
        tool, host = self.live_tool()
        with self.assertRaises(OSError):
            tool.language_install(self.pack)
        self.assertEqual(list(outside.iterdir()), [])
        self.lang.unlink()
        self.lang.mkdir()
        (self.lang / "fr.pack").symlink_to(self.pack)
        with self.assertRaises(OSError):
            tool.language_install(self.pack)
        self.assertEqual(host.ejected, [])

    def test_partial_write_preserves_the_existing_pack_and_cleans_its_stage(self):
        old = pack(name="Ancien")
        (self.lang / "fr.pack").write_bytes(old)
        tool, host = self.live_tool()
        original = os.write
        calls = []
        def partial(descriptor, data):
            calls.append(True)
            if len(calls) == 1:
                return original(descriptor, data[:5])
            raise OSError("simulated interrupted write")
        with mock.patch.object(echo_language.os, "write", side_effect=partial):
            with self.assertRaises(OSError):
                tool.language_install(self.pack)
        self.assertEqual((self.lang / "fr.pack").read_bytes(), old)
        self.assertEqual([item.name for item in self.lang.iterdir()], ["fr.pack"])
        self.assertEqual(host.ejected, [])

    def test_localized_language_title_cannot_redirect_to_another_setting(self):
        for label in ("Brightness", "bRIGHTNESS"):
            with self.subTest(label=label):
                (self.lang / "fr.pack").write_bytes(pack(STR_LANGUAGE=label))
                (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(settings("fr"))
                self.pack.write_bytes(pack("de", "5"))
                before = (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").read_bytes()
                tool, host = self.live_tool("de")
                with self.assertRaises(echo_debug.DebugError) as raised:
                    tool.language_install(self.pack, select=True)
                self.assertEqual(raised.exception.code, "language_choices_ambiguous")
                self.assertFalse((self.lang / "de.pack").exists())
                self.assertEqual((self.root / echo_debug.DATA_DIRECTORY / "settings.bin").read_bytes(), before)
                self.assertEqual(host.ejected, [])

    def test_native_names_cannot_alias_builtin_english_by_ascii_case(self):
        self.pack.write_bytes(pack("de", "ENGLISH"))
        tool, host = self.live_tool()
        with self.assertRaises(echo_debug.DebugError) as raised:
            tool.language_install(self.pack)
        self.assertEqual(raised.exception.code, "language_choices_ambiguous")
        self.assertFalse((self.lang / "de.pack").exists())
        (self.lang / "de.pack").write_bytes(self.pack.read_bytes())
        with self.assertRaises(echo_debug.DebugError) as raised:
            tool.language_select("de")
        self.assertEqual(raised.exception.code, "language_choices_ambiguous")
        self.assertEqual(host.ejected, [])

    def test_unsafe_current_label_cannot_publish_or_inject_a_script(self):
        (self.lang / "de.pack").write_bytes(pack("de", "Deutsch", STR_LANGUAGE="Sprache\nreconnect"))
        (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(settings("de"))
        tool, host = self.live_tool("fr")
        with self.assertRaises(echo_debug.DebugError) as raised:
            tool.language_install(self.pack, select=True)
        self.assertEqual(raised.exception.code, "language_script_unsafe")
        self.assertFalse((self.lang / "fr.pack").exists())
        self.assertEqual(host.ejected, [])

    def test_an_oversized_localized_selection_script_is_refused_before_publication(self):
        (self.lang / "de.pack").write_bytes(pack("de", "Deutsch", STR_LANGUAGE="S" * 700))
        (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(settings("de"))
        tool, host = self.live_tool("fr")
        with self.assertRaises(echo_debug.DebugError) as raised:
            tool.language_install(self.pack, select=True)
        self.assertEqual(raised.exception.code, "script_too_long")
        self.assertFalse((self.lang / "fr.pack").exists())
        self.assertEqual(host.ejected, [])

    def test_duplicate_native_names_and_ninth_language_fail_before_writes(self):
        tool, host = self.live_tool()
        (self.lang / "de.pack").write_bytes(pack("de", "Francais"))
        with self.assertRaises(echo_debug.DebugError) as raised:
            tool.language_install(self.pack, select=True)
        self.assertEqual(raised.exception.code, "language_choices_ambiguous")
        (self.lang / "de.pack").unlink()
        for index in range(7):
            code = "x" + str(index)
            (self.lang / (code + ".pack")).write_bytes(pack(code, "Native" + str(index)))
        with self.assertRaises(echo_debug.DebugError) as raised:
            tool.language_install(self.pack)
        self.assertEqual(raised.exception.code, "language_choices_ambiguous")
        self.assertFalse((self.lang / "fr.pack").exists())
        self.assertEqual(host.ejected, [])

    def test_ambiguous_or_unmounted_card_does_not_fall_back_to_internal(self):
        for media in ([medium("Internal storage", "ECHO", self.root),
                       {"_name": "SD card", "volumes": []}],
                      [medium("Internal storage", "ECHO", self.root),
                       medium("SD card", "MUSIC", self.card),
                       medium("SD card 2", "OTHER", self.card)]):
            with self.subTest(media=media):
                status, result = self.run_cli("language-list", host=FakeHost(usb_tree(player(media))))
                self.assertEqual(status, 1)
                self.assertIn(result["error"]["code"], ("language_volume_ambiguous", "language_volume_unmounted"))

    def test_language_guard_does_not_expand_debug_write_permissions(self):
        tool, _ = self.live_tool()
        with self.assertRaises(echo_debug.DebugError):
            tool.guarded(self.root, self.root / echo_debug.DATA_DIRECTORY / "lang" / "fr.pack")

    def test_casefold_code_collision_cannot_replace_another_fat_pack(self):
        (self.lang / "FR.pack").write_bytes(pack("FR", "Ancien"))
        tool, host = self.live_tool()
        with self.assertRaises(echo_debug.DebugError) as raised:
            tool.language_install(self.pack)
        self.assertEqual(raised.exception.code, "language_code_mismatch")
        self.assertEqual((self.lang / "FR.pack").read_bytes(), pack("FR", "Ancien"))
        self.assertEqual(host.ejected, [])

    def test_failed_stage_readback_keeps_the_existing_file_and_cleans_stage(self):
        old = pack(name="Ancien")
        (self.lang / "fr.pack").write_bytes(old)
        tool, host = self.live_tool()
        original = echo_language.read_at
        def failed(descriptor, name, limit):
            if name.endswith(".part"):
                raise OSError("simulated readback failure")
            return original(descriptor, name, limit)
        with mock.patch.object(echo_language, "read_at", side_effect=failed):
            with self.assertRaises(OSError):
                tool.language_install(self.pack)
        self.assertEqual((self.lang / "fr.pack").read_bytes(), old)
        self.assertEqual([item.name for item in self.lang.iterdir()], ["fr.pack"])
        self.assertEqual(host.ejected, [])

    def replay_gain_snapshot(self, source_version=14, mask="0xF8u", extra_mode=False):
        original = echo_language.Snapshot.show
        source = subprocess.check_output(["git", "show",
            "e3aa3955:firmware/src/settings/settings.c"], cwd=ROOT, text=True)
        source = source.replace("VERSION = 14,", "VERSION = " + str(source_version) + ",", 1)
        source = source.replace("0xF8u", mask)
        modes = subprocess.check_output(["git", "show",
            "e3aa3955:firmware/src/core/include/core/replay_gain.h"], cwd=ROOT, text=True)
        if extra_mode:
            modes = modes.replace("    REPLAY_GAIN_MODE_COUNT", "    REPLAY_GAIN_EXTRA,\n    REPLAY_GAIN_MODE_COUNT")
        def show(snapshot, path):
            if path == "firmware/src/settings/settings.c":
                return source
            if path == "firmware/src/core/include/core/replay_gain.h":
                return modes
            return original(snapshot, path)
        return mock.patch.object(echo_language.Snapshot, "show", show)

    def test_version14_replay_gain_flags_preserve_language_listing_and_selection(self):
        (self.lang / "fr.pack").write_bytes(pack(STR_LANGUAGE="Langue"))
        for flags in (0, 1, 2, 4, 5, 6):
            with self.subTest(flags=flags), self.replay_gain_snapshot():
                (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(settings("fr", 14, flags))
                tool, host = self.live_tool("en", version=14, flags=flags)
                self.assertEqual(tool.language_list()["selected"], "fr")
                self.assertEqual(tool.language_select("en")["selected"], "en")
                self.assertIn("set Langue=English\n", self.sent[0])
                self.assertEqual(host.ejected, [str(self.root), str(self.card)])
                data = (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").read_bytes()
                self.assertEqual((data[4], data[5]), (14, flags))

    def test_version14_rejects_reserved_replay_gain_flags_before_writes(self):
        for flags in (3, 7, 8, 128, 255):
            with self.subTest(flags=flags), self.replay_gain_snapshot():
                (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(settings("en", 14, flags))
                tool, host = self.live_tool()
                with self.assertRaises(echo_debug.DebugError) as raised:
                    tool.language_select("en")
                self.assertEqual(raised.exception.code, "language_settings_invalid")
                self.assertEqual(host.ejected, [])
                self.assertFalse((self.debug / "run.txt").exists())

    def test_older_language_settings_require_the_reserved_byte_to_stay_zero(self):
        for version in (12, 13):
            for flags in (0, 1, 4, 255):
                with self.subTest(version=version, flags=flags), self.replay_gain_snapshot():
                    (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(settings("en", version, flags))
                    tool, _ = self.live_tool()
                    if flags == 0:
                        self.assertEqual(tool.language_list()["selected"], "en")
                    else:
                        with self.assertRaises(echo_debug.DebugError) as raised:
                            tool.language_list()
                        self.assertEqual(raised.exception.code, "language_settings_invalid")

    def current_snapshot(self, home_items=None):
        original = echo_language.Snapshot.show
        sources = {path: (ROOT / path).read_text() for path in
                   ("firmware/src/settings/settings.c", "firmware/src/settings/include/settings/settings.h")}
        if home_items is not None:
            header = "firmware/src/settings/include/settings/settings.h"
            sources[header] = re.sub(r"SETTINGS_HOME_ITEM_COUNT = \d+", "SETTINGS_HOME_ITEM_COUNT = %d" % home_items,
                                     sources[header])
        def show(snapshot, path):
            return sources[path] if path in sources else original(snapshot, path)
        return mock.patch.object(echo_language.Snapshot, "show", show)

    def test_version15_settings_list_and_select_a_language(self):
        (self.lang / "fr.pack").write_bytes(pack(STR_LANGUAGE="Langue"))
        extension = bytes([0x12, 60, 60, 0, 0x78, 0x56, 0x34, 0x12, 0x00, 0xF7, 0x01])
        path = self.root / echo_debug.DATA_DIRECTORY / "settings.bin"
        with self.current_snapshot():
            path.write_bytes(settings("fr", 15, 5, extension + bytes(5)))
            tool, host = self.live_tool("en", version=15, flags=5)
            self.assertEqual(tool.language_list()["selected"], "fr")
            self.assertEqual(tool.language_select("en")["selected"], "en")
            self.assertEqual(host.ejected, [str(self.root), str(self.card)])
            data = path.read_bytes()
            self.assertEqual((data[4], data[5]), (15, 5))

    def home_extension(self, order, hidden=0):
        nibbles = bytearray(5)
        for index, item in enumerate(order):
            nibbles[index // 2] |= item << 4 * (index % 2)
        return bytes([0x07, 24, 15, 0]) + bytes(nibbles) + bytes([hidden & 255, hidden >> 8]) + bytes(5)

    def home_order_verifies(self, order, hidden=0, home_items=None):
        with self.current_snapshot(home_items):
            (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(
                settings("en", 15, 0, self.home_extension(order, hidden)))
            tool, _ = self.live_tool()
            try:
                tool.language_list()
            except echo_debug.DebugError as error:
                self.assertEqual(error.code, "language_settings_invalid")
                return False
            return True

    def test_a_home_order_follows_the_items_of_the_installed_build(self):
        nine = [8, 7, 6, 5, 4, 3, 2, 1, 0]
        ten = [9, 0, 1, 2, 3, 4, 5, 6, 7, 8]
        self.assertTrue(self.home_order_verifies(nine, 1 << 2))
        self.assertFalse(self.home_order_verifies(ten))
        self.assertFalse(self.home_order_verifies(nine, 1 << 9))
        self.assertTrue(self.home_order_verifies(nine, 1 << 2, home_items=10))
        self.assertTrue(self.home_order_verifies(ten, 1 << 9, home_items=10))
        self.assertFalse(self.home_order_verifies(nine, 1 << 9, home_items=10))
        self.assertFalse(self.home_order_verifies(ten, 1 << 3, home_items=10))
        self.assertFalse(self.home_order_verifies([7, 6, 5, 4, 3, 2, 1, 0], home_items=10))

    def test_an_unexpected_home_layout_fails_closed(self):
        with self.current_snapshot(home_items=11):
            tool, host = self.live_tool()
            with self.assertRaises(echo_debug.DebugError) as raised:
                tool.language_list()
            self.assertEqual(raised.exception.code, "language_build_unknown")
            self.assertEqual(host.ejected, [])

    def test_version15_settings_with_an_invalid_extension_cannot_be_verified(self):
        valid = bytes([0x07, 24, 15, 0, 0x10, 0x32, 0x54, 0x76, 0x08]) + bytes(7)
        for offset, value in ((0, 0x18), (1, 61), (2, 4), (3, 1), (4, 0x00), (8, 0x18), (9, 0x08),
                              (10, 0x02), (15, 1)):
            extension = bytearray(valid)
            extension[offset] = value
            with self.subTest(offset=offset, value=value), self.current_snapshot():
                (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(
                    settings("en", 15, 0, bytes(extension)))
                tool, host = self.live_tool()
                with self.assertRaises(echo_debug.DebugError) as raised:
                    tool.language_list()
                self.assertEqual(raised.exception.code, "language_settings_invalid")
                self.assertEqual(host.ejected, [])

    def test_future_or_changed_replay_gain_layouts_fail_closed(self):
        for options in ({"source_version": 15}, {"mask": "0xF0u"}, {"extra_mode": True}):
            with self.subTest(options=options), self.replay_gain_snapshot(**options):
                tool, host = self.live_tool()
                with self.assertRaises(echo_debug.DebugError) as raised:
                    tool.language_list()
                self.assertEqual(raised.exception.code, "language_build_unknown")
                self.assertEqual(host.ejected, [])

    def test_catalogue_upgrade_ignores_old_choices_and_replaces_the_saved_pack(self):
        old = PACK.build_pack("fr", CATALOGUE[:-1], {"STR_LANGUAGE_NAME": "Ancien"})
        other = PACK.build_pack("de", CATALOGUE[:-1], {"STR_LANGUAGE_NAME": "Deutsch"})
        (self.lang / "fr.pack").write_bytes(old)
        (self.lang / "de.pack").write_bytes(other)
        (self.root / echo_debug.DATA_DIRECTORY / "settings.bin").write_bytes(settings("fr"))
        tool, host = self.live_tool("fr")
        listed = tool.language_list()
        self.assertEqual([item["code"] for item in listed["languages"]], ["en"])
        self.assertEqual((listed["selected"], listed["saved_language"]), ("en", "fr"))
        self.assertEqual({item["code"] for item in listed["incompatible_files"]}, {"fr", "de"})
        self.assertEqual(tool.language_install(self.pack, select=True)["selected"], "fr")
        self.assertEqual((self.lang / "fr.pack.bak").read_bytes(), old)
        self.assertEqual((self.lang / "fr.pack").read_bytes(), pack())
        self.assertEqual((self.lang / "de.pack").read_bytes(), other)
        self.assertEqual(host.ejected, [str(self.root), str(self.card)])
        self.assertIn("set Language=Francais\n", self.sent[0])

    def damaged_body(self, code, checksum=True):
        data = bytearray(pack(code, "Ancien"))
        struct.pack_into("<H", data, PACK.HEADER.size, len(data))
        if checksum:
            checksum = PACK.fnv(data[PACK.CHECKSUM_END:], PACK.fnv(data[:PACK.CHECKSUM_OFFSET]))
            struct.pack_into("<I", data, PACK.CHECKSUM_OFFSET, checksum)
        return bytes(data)

    def test_an_explicit_exact_code_replacement_can_repair_a_damaged_body(self):
        for checksum in (False, True):
            with self.subTest(checksum=checksum):
                old = self.damaged_body("fr", checksum)
                (self.lang / "fr.pack").write_bytes(old)
                tool, host = self.live_tool("fr")
                self.assertEqual(tool.language_install(self.pack, select=True)["selected"], "fr")
                self.assertEqual((self.lang / "fr.pack.bak").read_bytes(), old)
                self.assertEqual((self.lang / "fr.pack").read_bytes(), pack())
                self.assertEqual(host.ejected, [str(self.root), str(self.card)])

    def test_an_unrelated_current_catalogue_damaged_body_remains_a_hard_error(self):
        damaged = self.damaged_body("de")
        (self.lang / "de.pack").write_bytes(damaged)
        tool, host = self.live_tool("fr")
        with self.assertRaises(echo_debug.DebugError) as raised:
            tool.language_install(self.pack, select=True)
        self.assertEqual(raised.exception.code, "language_pack_invalid")
        self.assertFalse((self.lang / "fr.pack").exists())
        self.assertEqual((self.lang / "de.pack").read_bytes(), damaged)
        self.assertEqual(host.ejected, [])

    def test_catalogue_mismatch_cannot_hide_a_wrong_filename_code_or_oversize_target(self):
        old = PACK.build_pack("de", CATALOGUE[:-1], {"STR_LANGUAGE_NAME": "Deutsch"})
        (self.lang / "fr.pack").write_bytes(old)
        tool, host = self.live_tool("fr")
        with self.assertRaises(echo_debug.DebugError) as raised:
            tool.language_install(self.pack)
        self.assertEqual(raised.exception.code, "language_code_mismatch")
        (self.lang / "fr.pack").write_bytes(b"x" * (echo_language.PACK_LIMIT + 1))
        with self.assertRaises(OSError):
            tool.language_install(self.pack)
        self.assertEqual(host.ejected, [])

    def test_mcp_parity_and_wrong_types_return_structured_errors(self):
        tool, _ = self.live_tool()
        listed = echo_debug_mcp.call_tool(tool, "echo_language_list", {})
        self.assertEqual(listed["structuredContent"], tool.language_list())
        for name, arguments in (("echo_language_install", {"pack": str(self.pack), "select": "yes"}),
                                ("echo_language_select", {"code": "en", "timeout": True}),
                                ("echo_language_list", {"music_volume": False})):
            with self.subTest(name=name):
                result = echo_debug_mcp.call_tool(tool, name, arguments)
                self.assertTrue(result["isError"])
                self.assertEqual(result["structuredContent"]["error"]["code"], "usage")


if __name__ == "__main__":
    unittest.main()
