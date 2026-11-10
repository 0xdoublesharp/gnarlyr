import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import echo_debug

SCRATCH = Path("/Volumes/Scratch/SnowskyEcho/tmp")
GIB = 1 << 30
SYSTEM_LINES = [
    "debug_report: 1",
    "firmware.name: Doublesharp Echo",
    "firmware.version: 1.0.0",
    "firmware.build: 1051212",
    "uptime_ms: 61000",
    "battery.millivolts: unknown",
    "fault.pc: 0x0305a1b2",
    "usb.last_end: ejected",
    "events.recorded: 2",
    "events.dropped: 0",
    "event: 371 screen 1 Doubles",
    "event: 1045 key play long",
    "profile.state: on",
    "profile.samples: 1000",
    "profile.idle: 300",
    "profile.overlay: 40",
    "profile.other: 10",
    "profile.code_base: 0x03050000",
    "profile.bucket_bytes: 128",
    "profile.buckets: 1229",
    "profile.row: 0 190 c8",
    "profile.row: 10 32",
]
NM_TEXT = "\n".join([
    "",
    "Symbols from echo_device.elf:",
    "",
    "Name                  Value   Class        Type         Size     Line  Section",
    "",
    "first_half          |03050000|   T  |              FUNC|00000040|     |.code\t/w/firmware/src/app/one.c:10",
    "second_half         |03050040|   t  |              FUNC|00000040|     |.code\t/w/firmware/src/app/two.c:20",
    "quarter             |03050080|   T  |              FUNC|00000020|     |.code\t/w/firmware/src/app/two.c:90",
    "library_function    |030500a0|   T  |              FUNC|00000020|     |.code",
    "TABLE               |030500c0|   t  |            OBJECT|00000040|     |.code",
    "music_ring          |03026dd8|   b  |            OBJECT|0001b580|     |.bss",
    "device_code_end     |03071234|   T  |            NOTYPE|        |     |.code",
])
MAP_TEXT = "\n".join([
    "Memory Configuration",
    "",
    "Linker script and memory map",
    "",
    ".code           0x03050000     0x1000",
    " .text.library_function",
    "                0x030500a0       0x20 logic/libecho_device_logic.a(pc_profile.c.obj)",
])


def report_text(lines=SYSTEM_LINES, truncated=0, pad=True):
    body = "\n".join(lines) + "\n"
    text = body + f"truncated: {truncated}\nend: {len(body)}\n"
    return text + "\n" * (echo_debug.REPORT_BYTES - len(text)) if pad else text


def temporary_directory():
    base = SCRATCH if os.path.ismount("/Volumes/Scratch") and SCRATCH.is_dir() else None
    return tempfile.TemporaryDirectory(dir=base, prefix="echo-debug-test-")


class FakeHost:
    def __init__(self, tree):
        self.tree = tree
        self.ejected = []

    def usb_tree(self):
        return self.tree

    def eject(self, mount_point):
        self.ejected.append(str(mount_point))


def medium(name, volume_name, mount_point, size=3 * GIB + 512):
    return {"_name": name, "size_in_bytes": size,
            "volumes": [{"_name": volume_name, "mount_point": str(mount_point), "size_in_bytes": size}]}


def player(media, name="Doublesharp Echo", vendor="0x1209", product="0x0001"):
    return {"_name": name, "vendor_id": vendor, "product_id": product, "serial_num": "1A2B3C4D",
            "Media": media}


def usb_tree(*devices):
    hub = {"_name": "USB 3.1 Bus", "_items": [{"_name": "Keyboard", "vendor_id": "0x05ac"}, *devices]}
    return {"SPUSBDataType": [hub]}


class VolumeCase(unittest.TestCase):
    def setUp(self):
        self.directory = temporary_directory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name) / "ECHO"
        self.debug = self.root / ".doublesharp" / "debug"
        self.debug.mkdir(parents=True)
        self.card = Path(self.directory.name) / "MUSIC"
        (self.card / ".doublesharp" / "debug").mkdir(parents=True)

    def write_report(self, text=None):
        (self.debug / "report.txt").write_text(report_text() if text is None else text, encoding="ascii")

    def host(self, *media):
        media = media or (medium("Internal storage", "ECHO", self.root),
                          medium("SD card", "MUSIC", self.card, 32 * GIB))
        return FakeHost(usb_tree(player(list(media))))

    def run_cli(self, *arguments, host=None):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            try:
                status = echo_debug.main(list(arguments), host=host)
            except SystemExit as exit_request:
                status = exit_request.code
        return status, json.loads(output.getvalue())


class ReportParsing(unittest.TestCase):
    def test_keys_become_values_and_unknown_becomes_none(self):
        report = echo_debug.parse_report(report_text())
        values = report["values"]
        self.assertEqual(report["schema"], 1)
        self.assertEqual(values["firmware.name"], "Doublesharp Echo")
        self.assertEqual(values["firmware.build"], "1051212")
        self.assertEqual(values["uptime_ms"], 61000)
        self.assertIsNone(values["battery.millivolts"])
        self.assertEqual(values["fault.pc"], 0x0305A1B2)
        self.assertEqual(values["usb.last_end"], "ejected")
        self.assertFalse(report["truncated"])
        self.assertEqual(report["unread_lines"], [])

    def test_a_build_tag_of_digits_stays_text_like_any_other_tag(self):
        lines = [line.replace("1051212", "1051212+") for line in SYSTEM_LINES]
        self.assertEqual(echo_debug.parse_report(report_text(lines))["values"]["firmware.build"], "1051212+")

    def test_events_keep_their_order_time_and_detail(self):
        events = echo_debug.parse_report(report_text())["events"]
        self.assertEqual(events, [{"time_ms": 371, "kind": "screen", "detail": "1 Doubles"},
                                  {"time_ms": 1045, "kind": "key", "detail": "play long"}])

    def test_histogram_rows_are_hex_runs_from_a_first_bucket(self):
        buckets = echo_debug.parse_report(report_text())["profile_buckets"]
        self.assertEqual(buckets, {"0": 0x190, "1": 0xC8, "16": 0x32})

    def test_a_truncated_report_is_read_and_flagged(self):
        self.assertTrue(echo_debug.parse_report(report_text(truncated=1))["truncated"])

    def test_a_file_without_the_schema_line_is_not_a_report(self):
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.parse_report("\0" * 64)
        self.assertEqual(raised.exception.code, "not_a_report")

    def test_another_schema_version_is_refused_not_guessed(self):
        lines = ["debug_report: 2"] + SYSTEM_LINES[1:]
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.parse_report(report_text(lines))
        self.assertEqual(raised.exception.code, "unsupported_schema")

    def test_a_report_without_its_end_line_is_incomplete(self):
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.parse_report("\n".join(SYSTEM_LINES) + "\n")
        self.assertEqual(raised.exception.code, "incomplete_report")

    def test_an_end_line_that_disagrees_with_the_length_is_incomplete(self):
        text = report_text().replace("uptime_ms: 61000", "uptime_ms: 6100")
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.parse_report(text)
        self.assertEqual(raised.exception.code, "incomplete_report")

    def test_text_after_the_end_line_is_refused(self):
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.parse_report(report_text(pad=False) + "event: 9 key play press\n")
        self.assertEqual(raised.exception.code, "incomplete_report")

    def test_a_bad_event_or_histogram_row_is_an_error(self):
        for line in ("event: soon key", "profile.row: zz 1", "profile.row: 5"):
            with self.assertRaises(echo_debug.DebugError) as raised:
                echo_debug.parse_report(report_text(SYSTEM_LINES + [line]))
            self.assertEqual(raised.exception.code, "bad_report")

    def test_a_line_without_a_key_is_kept_as_unread(self):
        report = echo_debug.parse_report(report_text(SYSTEM_LINES + ["stray words"]))
        self.assertEqual(report["unread_lines"], ["stray words"])


class Symbolising(unittest.TestCase):
    def setUp(self):
        self.report = echo_debug.parse_report(report_text())

    def test_only_functions_are_read_from_nm(self):
        names = [name for _, _, name, _ in echo_debug.parse_nm(NM_TEXT)]
        self.assertEqual(names, ["first_half", "second_half", "quarter", "library_function"])

    def test_a_bucket_is_shared_by_the_bytes_each_function_has_in_it(self):
        profile = echo_debug.symbolise(self.report, NM_TEXT)
        times = {row["name"]: row["ms"] for row in profile["functions"]}
        self.assertEqual(times["first_half (app/one.c)"], 200.0)
        self.assertEqual(times["second_half (app/two.c)"], 200.0)
        self.assertEqual(times["quarter (app/two.c)"], 100.0)
        self.assertEqual(times["library_function ((unknown file))"], 100.0)

    def test_functions_are_listed_longest_first_with_their_share(self):
        rows = echo_debug.symbolise(self.report, NM_TEXT)["functions"]
        self.assertEqual([row["ms"] for row in rows], sorted((row["ms"] for row in rows), reverse=True))
        self.assertEqual(rows[0]["percent"], 20.0)

    def test_time_is_summed_per_source_file(self):
        files = {row["name"]: row["ms"] for row in echo_debug.symbolise(self.report, NM_TEXT)["files"]}
        self.assertEqual(files["app/two.c"], 300.0)
        self.assertEqual(files["app/one.c"], 200.0)

    def test_the_linker_map_names_the_object_of_a_function_without_line_information(self):
        files = {row["name"]: row["ms"]
                 for row in echo_debug.symbolise(self.report, NM_TEXT, map_text=MAP_TEXT)["files"]}
        self.assertEqual(files["device_logic/pc_profile.c"], 100.0)

    def test_a_bucket_holding_no_function_is_reported_as_such(self):
        times = {row["name"]: row["ms"] for row in echo_debug.symbolise(self.report, NM_TEXT)["functions"]}
        self.assertEqual(times[echo_debug.UNATTRIBUTED], 50.0)

    def test_the_shares_add_up_to_the_samples(self):
        profile = echo_debug.symbolise(self.report, NM_TEXT)
        self.assertEqual(profile["shares_ms"], {"code": 650, "idle": 300, "overlay": 40, "other": 10})
        self.assertTrue(profile["consistent"])
        self.assertEqual(profile["samples"], 1000)
        self.assertEqual(round(sum(row["ms"] for row in profile["functions"])), 650)

    def test_shares_that_do_not_add_up_are_flagged(self):
        lines = [line.replace("profile.idle: 300", "profile.idle: 299") for line in SYSTEM_LINES]
        report = echo_debug.parse_report(report_text(lines))
        self.assertFalse(echo_debug.symbolise(report, NM_TEXT)["consistent"])

    def test_the_top_limit_cuts_the_lists(self):
        self.assertEqual(len(echo_debug.symbolise(self.report, NM_TEXT, top=2)["functions"]), 2)

    def test_a_bucket_outside_the_histogram_is_an_error(self):
        report = echo_debug.parse_report(report_text(SYSTEM_LINES + ["profile.row: 4cd 1"]))
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.symbolise(report, NM_TEXT)
        self.assertEqual(raised.exception.code, "bad_report")

    def test_a_report_without_profile_figures_cannot_be_symbolised(self):
        lines = [line for line in SYSTEM_LINES if not line.startswith("profile.")]
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.symbolise(echo_debug.parse_report(report_text(lines)), NM_TEXT)
        self.assertEqual(raised.exception.code, "no_profile")

    def test_an_elf_without_functions_is_an_error(self):
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.symbolise(self.report, "nothing here")
        self.assertEqual(raised.exception.code, "no_symbols")


class FindingThePlayer(VolumeCase):
    def test_the_player_is_found_by_its_usb_identity_and_its_internal_drive_by_name_and_size(self):
        status = echo_debug.Tool(self.host()).status()
        self.assertTrue(status["connected"])
        self.assertEqual(status["volume"], str(self.root))
        self.assertEqual(status["usb"]["serial"], "1A2B3C4D")
        self.assertEqual([volume["mount_point"] for volume in status["usb"]["card_volumes"]], [str(self.card)])
        self.assertFalse(status["report_present"])

    def test_no_player_on_usb_is_a_status_not_an_error(self):
        status = echo_debug.Tool(FakeHost(usb_tree())).status()
        self.assertEqual((status["ok"], status["connected"], status["volume"]), (True, False, None))

    def test_another_device_with_the_test_usb_id_is_not_the_player(self):
        tree = usb_tree(player([medium("Internal storage", "ECHO", self.root)], name="Some Board"))
        self.assertFalse(echo_debug.Tool(FakeHost(tree)).status()["connected"])

    def test_a_device_with_the_players_name_and_another_usb_id_is_not_the_player(self):
        tree = usb_tree(player([medium("Internal storage", "ECHO", self.root)], product="0x0002"))
        self.assertFalse(echo_debug.Tool(FakeHost(tree)).status()["connected"])

    def test_the_vendor_text_macos_appends_to_an_id_is_ignored(self):
        tree = usb_tree(player([medium("Internal storage", "ECHO", self.root)], vendor="0x1209  (pid.codes)"))
        self.assertTrue(echo_debug.Tool(FakeHost(tree)).status()["connected"])

    def assert_refused(self, host, code):
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.Tool(host).status()
        self.assertEqual(raised.exception.code, code)

    def test_the_sd_card_alone_is_never_taken_for_the_internal_drive(self):
        self.assert_refused(self.host(medium("SD card", "ECHO", self.card, 3 * GIB)),
                            "internal_drive_not_identified")

    def test_a_drive_the_tool_cannot_name_is_refused(self):
        self.assert_refused(self.host(medium("Mass Storage", "ECHO", self.root)),
                            "internal_drive_not_identified")

    def test_a_wrong_volume_name_is_refused(self):
        self.assert_refused(self.host(medium("Internal storage", "UNTITLED", self.root)), "wrong_volume_name")

    def test_a_volume_of_the_wrong_size_is_refused(self):
        self.assert_refused(self.host(medium("Internal storage", "ECHO", self.root, 64 * GIB)),
                            "wrong_volume_size")
        self.assert_refused(self.host(medium("Internal storage", "ECHO", self.root, 100 << 20)),
                            "wrong_volume_size")

    def test_an_unmounted_internal_drive_is_refused(self):
        unmounted = medium("Internal storage", "ECHO", self.root)
        unmounted["volumes"][0]["mount_point"] = None
        self.assert_refused(self.host(unmounted), "internal_drive_not_mounted")

    def test_two_players_at_once_are_refused(self):
        media = [medium("Internal storage", "ECHO", self.root)]
        self.assert_refused(FakeHost(usb_tree(player(media), player(media))), "several_players")

    def test_another_volume_name_can_be_asked_for(self):
        host = self.host(medium("Internal storage", "PLAYER", self.root))
        self.assertTrue(echo_debug.Tool(host, volume_name="PLAYER").status()["connected"])


class VolumeOverride(VolumeCase):
    def test_a_named_folder_with_the_data_folder_is_used_without_touching_usb(self):
        status = echo_debug.Tool(FakeHost(None), volume=self.root).status()
        self.assertEqual((status["connected"], status["source"], status["volume"]),
                         (True, "override", str(self.root)))

    def test_a_folder_without_the_players_data_folder_is_refused(self):
        other = Path(self.directory.name) / "other"
        other.mkdir()
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.Tool(FakeHost(None), volume=other).status()
        self.assertEqual(raised.exception.code, "not_a_player_volume")

    def test_a_missing_folder_is_refused(self):
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.Tool(FakeHost(None), volume=self.root / "nowhere").report()
        self.assertEqual(raised.exception.code, "volume_missing")


class Commands(VolumeCase):
    def test_report_reads_the_file_in_the_debug_folder(self):
        self.write_report()
        result = echo_debug.Tool(self.host()).report()
        self.assertTrue(result["ok"])
        self.assertEqual(result["path"], str(self.debug / "report.txt"))
        self.assertEqual(result["report"]["values"]["uptime_ms"], 61000)

    def test_the_card_is_never_read_even_when_it_holds_a_report(self):
        (self.card / ".doublesharp" / "debug" / "report.txt").write_text(report_text(), encoding="ascii")
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.Tool(self.host()).report()
        self.assertEqual(raised.exception.code, "no_report")

    def test_report_without_a_player_is_an_error(self):
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.Tool(FakeHost(usb_tree())).report()
        self.assertEqual(raised.exception.code, "player_not_connected")

    def test_an_oversized_report_file_is_refused(self):
        (self.debug / "report.txt").write_bytes(b"debug_report: 1\n" + b"\n" * (5 * echo_debug.REPORT_BYTES))
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.Tool(self.host()).report()
        self.assertEqual(raised.exception.code, "bad_report")

    def test_profile_symbolises_the_report_and_names_its_build(self):
        self.write_report()
        result = echo_debug.Tool(self.host()).profile("echo_device.elf", nm_text=NM_TEXT)
        self.assertEqual(result["firmware_build"], "1051212")
        self.assertEqual(result["profile"]["samples"], 1000)

    def test_profile_with_a_missing_elf_is_an_error(self):
        self.write_report()
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.Tool(self.host()).profile(self.root / "missing.elf", nm="nm")
        self.assertEqual(raised.exception.code, "elf_missing")

    def test_no_command_of_this_set_writes_or_ejects(self):
        self.write_report()
        host = self.host()
        before = sorted(str(path) for path in Path(self.directory.name).rglob("*"))
        tool = echo_debug.Tool(host)
        tool.status()
        tool.report()
        tool.profile("echo_device.elf", nm_text=NM_TEXT)
        self.assertEqual(sorted(str(path) for path in Path(self.directory.name).rglob("*")), before)
        self.assertEqual(host.ejected, [])


class CommandLine(VolumeCase):
    def test_status_prints_json(self):
        status, output = self.run_cli("status", host=self.host())
        self.assertEqual(status, 0)
        self.assertEqual((output["ok"], output["connected"], output["source"]), (True, True, "usb"))

    def test_the_volume_option_reads_a_folder(self):
        self.write_report()
        status, output = self.run_cli("--volume", str(self.root), "report", host=FakeHost(None))
        self.assertEqual(status, 0)
        self.assertEqual(output["report"]["values"]["firmware.version"], "1.0.0")

    def test_an_error_is_json_with_a_code_and_a_failing_status(self):
        status, output = self.run_cli("report", host=FakeHost(usb_tree()))
        self.assertEqual(status, 1)
        self.assertEqual(output, {"ok": False, "error": {
            "code": "player_not_connected", "message": output["error"]["message"]}})

    def test_a_usage_mistake_is_json_too(self):
        status, output = self.run_cli("profile", host=self.host())
        self.assertEqual(status, 2)
        self.assertEqual(output["error"]["code"], "usage")


if __name__ == "__main__":
    unittest.main()
