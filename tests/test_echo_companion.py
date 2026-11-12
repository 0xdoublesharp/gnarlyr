import struct
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path[:0] = [str(Path(__file__).resolve().parent), str(Path(__file__).resolve().parents[1] / "tools")]

import echo_debug
import echo_companion
import echo_debug_mcp
import echo_language
from test_echo_debug import VolumeCase, SYSTEM_LINES, report_text
from test_echo_debug_loop import Clock, PlayerHost, script_report

BUILD13 = "013b67f"
BUILD14 = "e3aa3955"
BUILD15 = "416bd163"


EXTENSION = bytes([0x02, 30, 40, 0, 0x01, 0x23, 0x45, 0x67, 0x08, 0x11, 0x01, 0, 0, 0, 0, 0])
F_BLOCK = bytes([0x07, 0x05]) + bytes(14)
SETTINGS_SOURCES = ("firmware/src/settings/settings.c", "firmware/src/settings/include/settings/settings.h",
                    "firmware/src/app/settings_screens.c", "firmware/src/core/include/core/replay_gain.h",
                    "firmware/assets/lang/en.txt")


def checked(data):
    data = bytearray(data)
    fnv = echo_language.pack_tools(echo_debug.ROOT).fnv
    field = 54 if data[4] >= 15 else 38
    struct.pack_into("<I", data, field, fnv(data[:field]))
    if data[4] >= 16:
        struct.pack_into("<I", data, 74, fnv(data[58:74]))
    return bytes(data)


def saved_settings(version=13, flags=0, language="en"):
    data = bytearray(4096)
    data[:4] = b"DSST"
    data[4:19] = bytes([version, flags, 7, 2, 0, 0, 0, 0, 0, 13, 2, 0, 35, 0, 0])
    data[19:19 + len(language)] = language.encode("ascii")
    data[24:28] = bytes([1, 0, 0, 1])
    data[28:38] = bytes([20] * 10)
    tail = 78 if version >= 16 else 58 if version >= 15 else 42
    if version >= 15:
        data[38:54] = EXTENSION
    if version >= 16:
        data[53] = 16
        data[58:74] = F_BLOCK
    data[tail:] = bytes(index % 251 for index in range(len(data) - tail))
    return checked(data)


def current_settings_source():
    original = echo_language.Snapshot.show
    sources = {path: (echo_debug.ROOT / path).read_text() for path in SETTINGS_SOURCES}
    def show(snapshot, path):
        return sources[path] if path in sources else original(snapshot, path)
    return mock.patch.object(echo_language.Snapshot, "show", show)


class CompanionSettings(VolumeCase):
    def setUp(self):
        super().setUp()
        self.build = BUILD13
        self.version = 13
        self.path = self.root / echo_debug.DATA_DIRECTORY / "settings.bin"
        self.install_fixture()

    def install_fixture(self, flags=0, language="en"):
        self.write_report(report_text([line.replace("1051212", self.build) for line in SYSTEM_LINES]))
        self.path.write_bytes(saved_settings(self.version, flags, language))

    def tool(self, mutate=None):
        self.sent = []
        def act():
            text = (self.debug / "run.txt").read_text()
            self.sent.append(text)
            if mutate is not None:
                self.path.write_bytes(checked(mutate(bytearray(self.path.read_bytes()))))
            self.write_report(script_report(text, build=self.build, screenshots=()))
        host = PlayerHost(self, act=act)
        clock = Clock()
        return echo_debug.Tool(host, sleep=clock.sleep, clock=clock.time), host

    def change(self, offset, value):
        def mutate(data):
            data[offset] = value
            return data
        return mutate

    def assert_no_write(self, host):
        self.assertEqual(host.ejected, [])
        self.assertFalse((self.debug / "run.txt").exists())

    def test_cli_lists_only_settings_the_installed_build_supports(self):
        status, result = self.run_cli("settings-list", host=self.host())
        self.assertEqual(status, 0)
        self.assertEqual(result["firmware_build"], BUILD13)
        self.assertEqual([entry["key"] for entry in result["settings"]],
                         ["text_size", "orientation", "brightness", "screen_timeout"])
        self.assertEqual(result["settings"][0]["value"], "medium")
        self.assertEqual([option["value"] for option in result["settings"][0]["options"]],
                         ["small", "medium", "large"])

    def test_setting_is_verified_after_reconnect_and_every_other_byte_is_preserved(self):
        before = self.path.read_bytes()
        tool, host = self.tool(self.change(24, 2))
        result = tool.settings_set("text_size", "large")
        self.assertTrue(result["ok"] and result["changed"] and result["verified"])
        self.assertEqual((result["setting"], result["before"], result["value"]),
                         ("text_size", "medium", "large"))
        expected = bytearray(before)
        expected[24] = 2
        self.assertEqual(self.path.read_bytes(), checked(expected))
        self.assertEqual(host.ejected, [str(self.root), str(self.card)])
        self.assertIn("set Text size=Large\n", self.sent[0])
        self.assertEqual(len(self.sent), 1)

    def test_a_localized_pack_supplies_the_actual_menu_and_option_text(self):
        snapshot = echo_language.Snapshot(echo_debug, self.build)
        data = snapshot.make_pack("fr",
                                         {"STR_LANGUAGE_NAME": "Français", "STR_TEXT_SIZE": "Taille du texte",
                                          "STR_SIZE_LARGE": "Grand"})
        folder = self.card / echo_debug.DATA_DIRECTORY / "lang"
        folder.mkdir()
        (folder / "fr.pack").write_bytes(data)
        self.install_fixture(language="fr")
        tool, _ = self.tool(self.change(24, 2))
        self.assertEqual(tool.settings_set("text_size", "large")["value"], "large")
        self.assertIn("set Taille du texte=Grand\n", self.sent[0])

    def test_failed_firmware_save_does_not_report_success_or_replay_a_mutation(self):
        before = self.path.read_bytes()
        tool, _ = self.tool()
        with self.assertRaises(echo_debug.DebugError) as refused:
            tool.settings_set("text_size", "large")
        self.assertEqual(refused.exception.code, "settings_not_applied")
        self.assertEqual(self.path.read_bytes(), before)
        self.assertEqual(len(self.sent), 1)

    def test_an_unrelated_settings_change_is_reported_without_a_second_write(self):
        def mutate(data):
            data[24] = 2
            data[13] = 30
            return data
        tool, _ = self.tool(mutate)
        with self.assertRaises(echo_debug.DebugError) as refused:
            tool.settings_set("text_size", "large")
        self.assertEqual(refused.exception.code, "settings_state_changed")
        self.assertIn("13", refused.exception.message)
        self.assertEqual(len(self.sent), 1)

    def test_an_already_selected_value_needs_no_write_or_usb_cycle(self):
        tool, host = self.tool()
        result = tool.settings_set("text_size", "medium")
        self.assertFalse(result["changed"])
        self.assertTrue(result["verified"])
        self.assert_no_write(host)

    def test_cli_and_mcp_share_results_and_reject_wrong_types(self):
        tool, _ = self.tool()
        expected = tool.settings_list()
        actual = echo_debug_mcp.call_tool(tool, "echo_settings_list", {})
        self.assertEqual(actual["structuredContent"], expected)
        for arguments in ({"setting": True, "value": "large"},
                          {"setting": "brightness", "value": 5},
                          {"setting": "text_size", "value": "large", "timeout": True}):
            with self.subTest(arguments=arguments):
                result = echo_debug_mcp.call_tool(tool, "echo_settings_set", arguments)
                self.assertTrue(result["isError"])
                self.assertEqual(result["structuredContent"]["error"]["code"], "usage")

    def test_cli_set_uses_canonical_values(self):
        tool, host = self.tool(self.change(6, 5))
        status, result = self.run_cli("settings-set", "brightness", "5", host=host)
        self.assertEqual(status, 0)
        self.assertEqual(result["value"], "5")
        self.assertIn("set Brightness=5\n", self.sent[0])

    def test_unknown_settings_options_and_injection_are_refused_before_writes(self):
        for setting, value in (("gain", "high"), ("usb_mode", "storage"),
                               ("text_size", "large\nreconnect"), ("brightness", "11"),
                               ("text_size", "Large"), ("replay_gain", "track")):
            with self.subTest(setting=setting, value=value):
                tool, host = self.tool()
                with self.assertRaises(echo_debug.DebugError):
                    tool.settings_set(setting, value)
                self.assert_no_write(host)

    def test_symlink_or_corrupt_settings_are_refused(self):
        original = self.path.read_bytes()
        self.path.unlink()
        outside = Path(self.directory.name) / "settings-outside.bin"
        outside.write_bytes(original)
        self.path.symlink_to(outside)
        tool, host = self.tool()
        with self.assertRaises(OSError):
            tool.settings_set("text_size", "large")
        self.assert_no_write(host)
        self.path.unlink()
        damaged = bytearray(original)
        damaged[24] = 2
        self.path.write_bytes(damaged)
        with self.assertRaises(echo_debug.DebugError):
            tool.settings_set("text_size", "large")
        self.assert_no_write(host)

    def test_invalid_existing_values_are_refused_before_a_firmware_default_reset(self):
        data = bytearray(self.path.read_bytes())
        data[6] = 0
        self.path.write_bytes(checked(data))
        tool, host = self.tool()
        with self.assertRaises(echo_debug.DebugError) as refused:
            tool.settings_set("text_size", "large")
        self.assertEqual(refused.exception.code, "settings_invalid")
        self.assert_no_write(host)

    def test_unknown_or_dirty_build_is_refused_before_writes(self):
        for build in (self.build + "+", "f" * 40):
            self.write_report(report_text([line.replace("1051212", build) for line in SYSTEM_LINES]))
            tool, host = self.tool()
            with self.assertRaises(echo_debug.DebugError):
                tool.settings_set("text_size", "large")
            self.assert_no_write(host)

    def test_changed_layout_is_refused_even_when_the_build_resolves(self):
        original = echo_language.Snapshot.show
        def show(snapshot, path):
            text = original(snapshot, path)
            return text.replace("TEXT_SIZE_FIELD = 24", "TEXT_SIZE_FIELD = 25")
        tool, host = self.tool()
        with mock.patch.object(echo_language.Snapshot, "show", show):
            with self.assertRaises(echo_debug.DebugError) as refused:
                tool.settings_set("text_size", "large")
        self.assertEqual(refused.exception.code, "settings_layout_unknown")
        self.assert_no_write(host)

    def test_locale_aliases_cannot_route_an_allowlisted_command_to_analog_gain(self):
        snapshot = echo_language.Snapshot(echo_debug, self.build)
        translations = {"STR_LANGUAGE_NAME": "Français", "STR_TEXT_SIZE": "Gain"}
        folder = self.card / echo_debug.DATA_DIRECTORY / "lang"
        folder.mkdir()
        (folder / "fr.pack").write_bytes(snapshot.make_pack("fr", translations))
        self.install_fixture(language="fr")
        tool, host = self.tool()
        with self.assertRaises(echo_debug.DebugError) as refused:
            tool.settings_set("text_size", "large")
        self.assertEqual(refused.exception.code, "settings_choices_ambiguous")
        self.assert_no_write(host)

    def test_duplicate_translated_option_labels_are_refused(self):
        snapshot = echo_language.Snapshot(echo_debug, self.build)
        translations = {"STR_LANGUAGE_NAME": "Français", "STR_SIZE_SMALL": "Même", "STR_SIZE_LARGE": "Même"}
        folder = self.card / echo_debug.DATA_DIRECTORY / "lang"
        folder.mkdir()
        (folder / "fr.pack").write_bytes(snapshot.make_pack("fr", translations))
        self.install_fixture(language="fr")
        tool, host = self.tool()
        with self.assertRaises(echo_debug.DebugError) as refused:
            tool.settings_set("text_size", "large")
        self.assertEqual(refused.exception.code, "settings_choices_ambiguous")
        self.assert_no_write(host)

    def test_matching_volume_override_still_uses_the_real_usb_refresh(self):
        tool, host = self.tool(self.change(26, 1))
        tool.volume = self.root
        self.assertEqual(tool.settings_set("orientation", "portrait")["value"], "portrait")
        self.assertEqual(host.ejected, [str(self.root), str(self.card)])

    def test_the_existing_debug_write_guard_stays_narrow(self):
        tool, _ = self.tool()
        with self.assertRaises(echo_debug.DebugError):
            tool.guarded(self.root, self.path)

    def test_older_saved_settings_cannot_be_silently_migrated_by_a_set(self):
        self.path.write_bytes(saved_settings(12))
        tool, host = self.tool()
        with self.assertRaises(echo_debug.DebugError) as refused:
            tool.settings_set("text_size", "large")
        self.assertEqual(refused.exception.code, "settings_migration_required")
        self.assert_no_write(host)

    def test_replay_gain_is_supported_only_for_the_declared_version14_layout(self):
        self.build, self.version = BUILD14, 14
        self.install_fixture(flags=4)
        tool, host = self.tool(self.change(5, 6))
        listed = tool.settings_list()
        self.assertEqual([entry["key"] for entry in listed["settings"]][-2:],
                         ["replay_gain", "prevent_clipping"])
        result = tool.settings_set("replay_gain", "album")
        self.assertEqual(result["value"], "album")
        self.assertEqual(self.path.read_bytes()[5], 6)
        self.assertIn("set ReplayGain=Album\n", self.sent[0])
        self.assertEqual(len(host.ejected), 2)

    def test_auto_replay_gain_is_offered_only_when_the_installed_build_declares_it(self):
        self.use_current_layout()
        original = echo_language.Snapshot.show
        paths = (*SETTINGS_SOURCES, "firmware/src/app/settings_screens.c",
                 "firmware/src/core/include/core/replay_gain.h", "firmware/assets/lang/en.txt")
        sources = {path: (echo_debug.ROOT / path).read_text() for path in paths}
        def show(snapshot, path):
            return sources[path] if path in sources else original(snapshot, path)
        with mock.patch.object(echo_language.Snapshot, "show", show):
            tool, _ = self.tool(self.change(5, 7))
            result = tool.settings_set("replay_gain", "auto")
            self.assertEqual(result["value"], "auto")
            self.assertEqual(self.path.read_bytes()[5], 7)
            self.assertEqual(self.path.read_bytes()[38:53], EXTENSION[:15])
            self.assertIn("set ReplayGain=Auto\n", self.sent[0])

    def test_prevent_clipping_preserves_replay_gain_mode_bits(self):
        self.build, self.version = BUILD14, 14
        self.install_fixture(flags=6)
        tool, _ = self.tool(self.change(5, 2))
        result = tool.settings_set("prevent_clipping", "off")
        self.assertEqual(result["value"], "off")
        self.assertEqual(self.path.read_bytes()[5], 2)


    def use_current_layout(self):
        self.build, self.version = BUILD14, 16
        self.install_fixture(flags=4)
        patcher = current_settings_source()
        patcher.start()
        self.addCleanup(patcher.stop)

    def refused_before_writes(self, data, command=("settings_set", "text_size", "large")):
        self.path.write_bytes(data)
        tool, host = self.tool()
        with self.assertRaises(echo_debug.DebugError) as refused:
            getattr(tool, command[0])(*command[1:])
        self.assertIn(refused.exception.code, ("settings_invalid", "language_settings_invalid"))
        self.assert_no_write(host)

    def test_version15_build_changes_a_version15_file_and_keeps_its_extension(self):
        self.build, self.version = BUILD15, 15
        self.install_fixture(flags=4)
        before = self.path.read_bytes()
        tool, _ = self.tool(self.change(24, 2))
        self.assertEqual(tool.settings_list()["settings_version"], 15)
        self.assertTrue(tool.settings_set("text_size", "large")["verified"])
        after = self.path.read_bytes()
        expected = bytearray(before)
        expected[24] = 2
        self.assertEqual(after, checked(expected))
        self.assertEqual(after[38:54], EXTENSION)
        self.assertEqual(after[58:], before[58:])

    def test_version15_build_refuses_a_version16_file(self):
        self.build, self.version = BUILD15, 15
        self.install_fixture(flags=4)
        self.refused_before_writes(saved_settings(16, 4))

    def test_version16_settings_change_preserves_the_extension_and_f_blocks(self):
        self.use_current_layout()
        before = self.path.read_bytes()
        tool, _ = self.tool(self.change(24, 2))
        self.assertEqual(tool.settings_list()["settings_version"], 16)
        result = tool.settings_set("text_size", "large")
        self.assertTrue(result["ok"] and result["changed"] and result["verified"])
        after = self.path.read_bytes()
        expected = bytearray(before)
        expected[24] = 2
        self.assertEqual(after, checked(expected))
        self.assertEqual(after[38:54], EXTENSION[:15] + bytes([16]))
        self.assertEqual(after[58:78], before[58:78])
        self.assertEqual(after[58:74], F_BLOCK)
        self.assertEqual(after[78:], before[78:])
        self.assertNotEqual(after[54:58], before[54:58])

    def test_a_companion_change_recomputes_only_the_base_checksum(self):
        self.use_current_layout()
        before = self.path.read_bytes()
        layout = echo_companion.Layout(echo_language.Snapshot(echo_debug, self.build))
        spec = next(item for item in layout.specs if item[0] == "text_size")
        changed = layout.changed(spec, "large", before)
        self.assertEqual([index for index in range(len(before)) if changed[index] != before[index]][0], 24)
        self.assertEqual(changed[54:], checked(changed)[54:])
        self.assertEqual(changed[58:], before[58:])

    def test_version16_replay_gain_change_keeps_the_extension_and_f_blocks(self):
        self.use_current_layout()
        before = self.path.read_bytes()
        tool, _ = self.tool(self.change(5, 6))
        self.assertEqual(tool.settings_set("replay_gain", "album")["value"], "album")
        data = self.path.read_bytes()
        self.assertEqual((data[5], data[38:53], data[58:78]), (6, EXTENSION[:15], before[58:78]))

    def test_version16_f_values_the_firmware_allocates_are_accepted(self):
        self.use_current_layout()
        for f0, f1 in ((0, 0), (0x01, 0), (0x02, 7), (0x0B, 3), (0x04, 1)):
            with self.subTest(f0=f0, f1=f1):
                data = bytearray(saved_settings(16, 4))
                data[58:60] = bytes([f0, f1])
                self.path.write_bytes(checked(data))
                tool, _ = self.tool()
                self.assertEqual(tool.settings_list()["settings_version"], 16)

    def test_version16_bad_f_checksum_is_refused_before_writes(self):
        self.use_current_layout()
        for offset in (58, 65, 73, 74, 77):
            with self.subTest(offset=offset):
                data = bytearray(saved_settings(16, 4))
                data[offset] ^= 1
                self.refused_before_writes(bytes(data))

    def test_version16_unallocated_or_out_of_range_f_bits_are_refused_before_writes(self):
        self.use_current_layout()
        for offset, value in ((58, 0x10), (58, 0x80), (58, 0x0C), (58, 0x0F), (59, 0x08), (59, 0x80),
                              (60, 62), (66, 0x40), (73, 0x80)):
            with self.subTest(offset=offset, value=value):
                data = bytearray(saved_settings(16, 4))
                data[offset] = value
                self.refused_before_writes(checked(data))

    def test_version16_wrong_extension_marker_is_refused_before_writes(self):
        self.use_current_layout()
        for value in (0, 15, 17, 0x80):
            with self.subTest(value=value):
                data = bytearray(saved_settings(16, 4))
                data[53] = value
                self.refused_before_writes(checked(data))

    def test_a_short_version16_file_is_refused_before_writes(self):
        self.use_current_layout()
        for length in (58, 74, 77):
            with self.subTest(length=length):
                self.refused_before_writes(saved_settings(16, 4)[:length])

    def test_a_version15_marker_must_stay_zero_under_the_current_build(self):
        self.use_current_layout()
        data = bytearray(saved_settings(15, 4))
        data[53] = 16
        self.refused_before_writes(checked(data), ("settings_list",))

    def test_a_changed_f_layout_is_refused(self):
        self.use_current_layout()
        original = echo_language.Snapshot.show
        for old, new in (("bytes + 74", "bytes + 75"), ("bytes[53] = 16", "bytes[52] = 16"),
                         ("SETTINGS_FILE_SIZE = 78", "SETTINGS_FILE_SIZE = 80"),
                         ("(bytes[59] & 0xF8u) == 0", "(bytes[59] & 0xF0u) == 0")):
            def show(snapshot, path, old=old, new=new):
                return original(snapshot, path).replace(old, new)
            with self.subTest(old=old), mock.patch.object(echo_language.Snapshot, "show", show):
                tool, host = self.tool()
                with self.assertRaises(echo_debug.DebugError) as refused:
                    tool.settings_list()
                self.assertEqual(refused.exception.code, "language_build_unknown")
                self.assert_no_write(host)

    def test_a_saved_usb_dac_answer_and_usb_power_bits_load_while_plugging_in_stays_on_the_player(self):
        self.use_current_layout()
        data = bytearray(self.path.read_bytes())
        data[5] |= 0x30
        data[52] = 0x10
        self.path.write_bytes(checked(data))
        tool, _ = self.tool()
        listed = {item["key"] for item in tool.settings_list()["settings"]}
        self.assertNotIn("battery_care", listed)
        self.assertIn("text_size", listed)

    def test_clock_format_uses_the_extension_bit_and_the_installed_choices(self):
        self.use_current_layout()
        original = echo_language.Snapshot.show
        paths = ("firmware/assets/lang/en.txt", "firmware/src/app/settings_screens.c")
        def show(snapshot, path):
            return (echo_debug.ROOT / path).read_text() if path in paths else original(snapshot, path)
        with mock.patch.object(echo_language.Snapshot, "show", show):
            before = self.path.read_bytes()
            tool, _ = self.tool(self.change(38, before[38] | 4))
            listed = {item["key"]: item for item in tool.settings_list()["settings"]}
            self.assertIn("clock_24h", listed)
            self.assertEqual(listed["clock_24h"]["value"], "12h")
            self.assertEqual(tool.settings_set("clock_24h", "24h")["value"], "24h")
            expected = bytearray(before)
            expected[38] |= 4
            self.assertEqual(self.path.read_bytes(), checked(expected))
            self.assertIn("set Clock format=24-hour\n", self.sent[0])

    def test_status_bar_settings_use_e13_and_preserve_the_reserved_clock_bytes(self):
        self.use_current_layout()
        original = echo_language.Snapshot.show
        paths = ("firmware/assets/lang/en.txt", "firmware/src/app/settings_screens.c")
        def show(snapshot, path):
            return (echo_debug.ROOT / path).read_text() if path in paths else original(snapshot, path)
        choices = (("status_volume", "on", 1), ("status_repeat", "on", 2),
                   ("status_position", "on", 4),
                   ("battery_style", "segmented", 8), ("battery_percent", "on", 0))
        with mock.patch.object(echo_language.Snapshot, "show", show):
            for key, value, bit in choices:
                with self.subTest(key=key):
                    before = bytearray(saved_settings(16, 4))
                    before[51] = 0xA0
                    self.path.write_bytes(checked(before))
                    def change(data):
                        data[51] |= bit
                        if key == "battery_percent":
                            data[5] |= 8
                        return data
                    tool, _ = self.tool(change)
                    listed = {item["key"]: item for item in tool.settings_list()["settings"]}
                    self.assertIn(key, listed)
                    self.assertEqual(tool.settings_set(key, value)["value"], value)
                    self.assertEqual(self.path.read_bytes()[51], 0xA0 | bit)
                    self.assertEqual(self.path.read_bytes()[49:51], bytes(2))

    def test_version16_extension_values_the_firmware_rejects_are_refused_before_writes(self):
        self.use_current_layout()
        cases = ((38, 0x18), (38, 0x20), (39, 61), (40, 4), (40, 61), (41, 7), (41, 0xFD), (42, 0x00),
                 (46, 0x18), (47, 0x08), (48, 0x02), (50, 4), (53, 0x80))
        for offset, value in cases:
            with self.subTest(offset=offset, value=value):
                data = bytearray(saved_settings(16, 4))
                data[offset] = value
                self.refused_before_writes(checked(data))

    def test_clock_offset_is_preserved_when_another_setting_changes(self):
        self.use_current_layout()
        before = bytearray(self.path.read_bytes())
        struct.pack_into("<h", before, 49, -480)
        self.path.write_bytes(checked(before))
        tool, _ = self.tool(self.change(24, 2))
        self.assertTrue(tool.settings_set("text_size", "large")["verified"])
        self.assertEqual(struct.unpack_from("<h", self.path.read_bytes(), 49)[0], -480)

    def test_the_base_checksum_covers_the_extension_block_and_marker(self):
        self.use_current_layout()
        data = bytearray(self.path.read_bytes())
        data[53] ^= 1
        self.path.write_bytes(data)
        tool, host = self.tool()
        with self.assertRaises(echo_debug.DebugError) as refused:
            tool.settings_list()
        self.assertIn(refused.exception.code, ("settings_invalid", "language_settings_invalid"))
        self.assert_no_write(host)

    def test_older_files_under_version16_firmware_require_a_player_save(self):
        self.use_current_layout()
        for version in (14, 15):
            with self.subTest(version=version):
                self.path.write_bytes(saved_settings(version, 4))
                tool, host = self.tool()
                with self.assertRaises(echo_debug.DebugError) as refused:
                    tool.settings_set("text_size", "large")
                self.assertEqual(refused.exception.code, "settings_migration_required")
                self.assert_no_write(host)

    def test_a_changed_extension_layout_is_refused(self):
        self.use_current_layout()
        original = echo_language.Snapshot.show
        def show(snapshot, path):
            text = original(snapshot, path)
            return text.replace("EXTENSION_CHECKSUM_FIELD = 54", "EXTENSION_CHECKSUM_FIELD = 55")
        tool, host = self.tool()
        with mock.patch.object(echo_language.Snapshot, "show", show):
            with self.assertRaises(echo_debug.DebugError):
                tool.settings_list()
        self.assert_no_write(host)

    def test_the_usb_answer_bits_survive_a_flags_change_with_or_without_battery_percent(self):
        self.build, self.version = BUILD14, 14
        original = echo_language.Snapshot.show
        old = "bytes[VERSION_FIELD + 1] = settings->replay_gain_mode | (settings->prevent_clipping ? 4u : 0u);"
        for extra, mask in (("", "0xC8u"), (" | (settings->battery_percent ? 8u : 0u)", "0xC0u")):
            encoder = old[:-1] + extra + " |\n        (uint8_t)(settings->usb_connect << 4u);"
            def show(snapshot, path, encoder=encoder, mask=mask):
                text = original(snapshot, path)
                if path.endswith("src/settings/settings.c"):
                    self.assertIn(old, text)
                    return text.replace(old, encoder).replace("0xF8u", mask)
                return text
            with self.subTest(mask=mask), mock.patch.object(echo_language.Snapshot, "show", show):
                self.install_fixture(flags=4 | 32)
                tool, _ = self.tool(self.change(5, 32))
                self.assertEqual(tool.settings_set("prevent_clipping", "off")["value"], "off")
                self.assertEqual(self.path.read_bytes()[5], 32)
                (self.debug / "run.txt").unlink(missing_ok=True)
                self.install_fixture(flags=4 | 48)
                tool, host = self.tool()
                with self.assertRaises(echo_debug.DebugError) as refused:
                    tool.settings_set("prevent_clipping", "off")
                self.assertEqual(refused.exception.code, "language_settings_invalid")
                self.assert_no_write(host)

    def test_changed_option_metadata_is_refused_before_writes(self):
        original = echo_language.Snapshot.show
        def show(snapshot, path):
            text = original(snapshot, path)
            if path.endswith("settings_screens.c"):
                return text.replace("{STR_SIZE_SMALL, STR_SIZE_MEDIUM,", "{STR_SIZE_LARGE, STR_SIZE_MEDIUM,")
            return text
        tool, host = self.tool()
        with mock.patch.object(echo_language.Snapshot, "show", show):
            with self.assertRaises(echo_debug.DebugError) as refused:
                tool.settings_set("text_size", "large")
        self.assertEqual(refused.exception.code, "settings_layout_unknown")
        self.assert_no_write(host)

    def test_a_concurrent_preferences_change_is_detected_before_sending(self):
        original = echo_language.Manager.recheck
        def recheck(manager, context):
            newer = original(manager, context)
            data = bytearray(self.path.read_bytes())
            data[6] = 5
            self.path.write_bytes(checked(data))
            return newer
        tool, host = self.tool()
        with mock.patch.object(echo_language.Manager, "recheck", recheck):
            with self.assertRaises(echo_debug.DebugError) as refused:
                tool.settings_set("text_size", "large")
        self.assertEqual(refused.exception.code, "settings_conflict")
        self.assertEqual(self.path.read_bytes()[6], 5)
        self.assert_no_write(host)

    def test_long_localized_titles_cannot_publish_an_oversized_script(self):
        snapshot = echo_language.Snapshot(echo_debug, self.build)
        translations = {"STR_LANGUAGE_NAME": "Français", "STR_TEXT_SIZE": "T" * 700}
        folder = self.card / echo_debug.DATA_DIRECTORY / "lang"
        folder.mkdir()
        (folder / "fr.pack").write_bytes(snapshot.make_pack("fr", translations))
        self.install_fixture(language="fr")
        tool, host = self.tool()
        with self.assertRaises(echo_debug.DebugError) as refused:
            tool.settings_set("text_size", "large")
        self.assertEqual(refused.exception.code, "script_too_long")
        self.assert_no_write(host)


if __name__ == "__main__":
    unittest.main()
