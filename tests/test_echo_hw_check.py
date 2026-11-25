import contextlib
import io
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

if __package__:
    from . import test_echo_debug as fixtures
else:
    import test_echo_debug as fixtures

import echo_debug
import echo_hw_check


CLI = Path(__file__).resolve().parents[1] / "tools" / "echo_hw_check.py"
BUILD = "e12ab45"
HEALTHY = {
    "debug_report": 1,
    "report.sequence": 12,
    "report.reason": "usb_session",
    "report.cold_start": 0,
    "firmware.build": BUILD,
    "fault.cfsr": "0x00000000",
    "fault.hfsr": "0x00000000",
    "fault.pc": "0x00000000",
    "audio.output_underruns": 0,
    "usb.writes_after_eject": 0,
    "start.last_step_finished": 1,
    "battery.millivolts": 3960,
    "events.dropped": 0,
}


def run_cli(*arguments, host=None):
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        status = echo_hw_check.main(arguments, host=host)
    return status, output.getvalue()


class ReportChecks(unittest.TestCase):
    def setUp(self):
        self.directory = fixtures.temporary_directory()
        self.addCleanup(self.directory.cleanup)
        self.volume = Path(self.directory.name) / "ECHO"
        self.debug = self.volume / echo_debug.DATA_DIRECTORY / echo_debug.DEBUG_DIRECTORY
        self.debug.mkdir(parents=True)

    def write_report(self, slot=0, truncated=0, *, omit=(), **values):
        lines = [f"{key}: {value}" for key, value in (HEALTHY | values).items() if key not in omit]
        path = self.debug / f"report{slot}.txt"
        path.write_text(fixtures.report_text(lines, truncated=truncated), encoding="ascii")
        return path

    def check(self, *arguments):
        status, output = run_cli("check", "--volume", str(self.volume), "--json", *arguments)
        return status, json.loads(output)

    def assert_failed_check(self, name, value, *arguments):
        status, result = self.check(*arguments)
        self.assertEqual(status, 1)
        self.assertFalse(result["ok"])
        failures = {check["name"]: check["value"] for check in result["checks"] if not check["ok"]}
        self.assertEqual(failures[name], value, failures)

    def test_healthy_report_passes_at_the_shell(self):
        path = self.write_report()
        result = subprocess.run([sys.executable, str(CLI), "check", "--volume", str(self.volume),
                                 "--build", BUILD], capture_output=True, text=True)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PASS", result.stdout)
        self.assertIn(path.name, result.stdout)
        self.assertIn("audio.output_underruns", result.stdout)

    def test_healthy_report_passes_as_json(self):
        path = self.write_report()
        status, result = self.check("--build", BUILD.upper())

        self.assertEqual(status, 0)
        self.assertTrue(result["ok"])
        self.assertEqual(result["path"], str(path))
        self.assertTrue(all(check["ok"] for check in result["checks"]))

    def test_nonzero_faults_underruns_late_writes_and_unfinished_start_fail(self):
        for name, raw, parsed in (
            ("fault.cfsr", "0x00000004", 4),
            ("fault.hfsr", "0x40000000", 0x40000000),
            ("fault.pc", "0x0305a1b2", 0x0305A1B2),
            ("audio.output_underruns", 1, 1),
            ("usb.writes_after_eject", 2, 2),
            ("start.last_step_finished", 0, 0),
        ):
            with self.subTest(name=name):
                self.write_report(**{name: raw})
                self.assert_failed_check(name, parsed)

    def test_text_failure_names_the_check_and_actual_value(self):
        self.write_report(**{"usb.writes_after_eject": 5})
        status, output = run_cli("check", "--volume", str(self.volume))

        self.assertEqual(status, 1)
        self.assertIn("FAIL usb.writes_after_eject = 5", output)

    def test_faults_must_be_known_and_underruns_must_be_a_nonnegative_integer(self):
        self.write_report(omit=("fault.hfsr",), **{"fault.cfsr": "unknown", "audio.output_underruns": -1})
        self.assert_failed_check("fault.cfsr", None)
        self.assert_failed_check("fault.hfsr", None)
        self.assert_failed_check("audio.output_underruns", -1)
        self.write_report(**{"audio.output_underruns": "bad"})
        self.assert_failed_check("audio.output_underruns", "bad")
        self.write_report(**{
            "start.warm": 0,
            "fault.cfsr": "unknown",
            "fault.hfsr": "unknown",
            "fault.pc": "unknown",
        })
        self.assertEqual(self.check()[0], 0)

    def test_the_underrun_limit_is_inclusive(self):
        self.write_report(**{"audio.output_underruns": 2})

        self.assertEqual(self.check("--max-underruns", "2")[0], 0)
        self.assert_failed_check("audio.output_underruns", 2, "--max-underruns", "1")

    def test_unknown_and_omitted_optional_fields_pass(self):
        self.write_report(omit=("start.last_step_finished", "events.dropped"),
                          **{"usb.writes_after_eject": "unknown", "battery.millivolts": "unknown"})

        self.assertEqual(self.check()[0], 0)

    def test_battery_range_and_dropped_events_are_gates(self):
        for millivolts in (3000, 4500):
            with self.subTest(millivolts=millivolts):
                self.write_report(**{"battery.millivolts": millivolts})
                self.assertEqual(self.check()[0], 0)
        for millivolts in (2999, 4501, "bad"):
            with self.subTest(millivolts=millivolts):
                self.write_report(**{"battery.millivolts": millivolts, "events.dropped": 1})
                self.assert_failed_check("battery.millivolts", millivolts)
                self.assert_failed_check("events.dropped", 1)

    def test_truncated_and_empty_cold_start_reports_fail(self):
        self.write_report(truncated=1)
        self.assert_failed_check("report.truncated", True)
        self.write_report(omit=("fault.cfsr", "fault.hfsr", "fault.pc"), **{"report.cold_start": 1})
        self.assert_failed_check("report.cold_start", 1)

    def test_mismatched_and_dirty_builds_fail(self):
        self.write_report(**{"firmware.build": "0000000"})
        self.assert_failed_check("firmware.build", "0000000", "--build", BUILD)
        self.write_report(**{"firmware.build": BUILD + "+"})
        self.assert_failed_check("firmware.build_clean", BUILD + "+", "--build", BUILD)
        self.assert_failed_check("firmware.build_clean", BUILD + "+")

    def test_recent_reports_are_ordered_by_sequence_and_cannot_mask_a_failure(self):
        self.write_report(slot=0, **{"report.sequence": 8})
        newest = self.write_report(slot=1, **{"report.sequence": 10, "fault.pc": 1})
        self.write_report(slot=2, **{"report.sequence": 9, "firmware.build": "0000000"})
        os.utime(newest, (0, 0))
        status, result = self.check("--build", BUILD)

        self.assertEqual(status, 1)
        self.assertEqual(result["path"], str(newest))
        self.assertEqual([item["sequence"] for item in result["reports"]], [10, 9, 8])
        self.assertEqual([check["name"] for check in result["checks"] if not check["ok"]], ["fault.pc"])

    def test_a_torn_slot_fails_even_when_another_slot_is_readable(self):
        self.write_report()
        (self.debug / "report1.txt").write_text("debug_report: 1\n", encoding="ascii")
        status, result = self.check()

        self.assertEqual(status, 1)
        failure = next(check for check in result["checks"] if not check["ok"])
        self.assertEqual(failure["name"], "report.readable")
        self.assertEqual(failure["value"][0]["file"], "report1.txt")
        self.assertEqual(failure["value"][0]["error"], "incomplete_report")

    def test_missing_reports_and_io_errors_fail_with_a_code(self):
        status, result = self.check()
        self.assertEqual(status, 1)
        self.assertEqual(result["checks"][0]["value"][0]["error"], "no_report")
        with patch.object(echo_debug.Tool, "read_reports", side_effect=PermissionError("denied")):
            status, result = self.check()
        self.assertEqual(status, 1)
        self.assertEqual(result["checks"][0]["value"][0]["error"], "io_error")

    def test_usb_discovery_reads_reports_without_writes_or_ejection(self):
        path = self.write_report()
        medium = fixtures.medium("Internal storage", "ECHO", self.volume)
        host = fixtures.FakeHost(fixtures.usb_tree(fixtures.player([medium])))
        original = path.read_bytes()

        status, output = run_cli("check", "--json", host=host)

        self.assertEqual(status, 0)
        self.assertEqual(json.loads(output)["path"], str(path))
        self.assertEqual(host.ejected, [])
        self.assertEqual(list(self.debug.iterdir()), [path])
        self.assertEqual(path.read_bytes(), original)

    def test_usb_dac_stats_pass_when_clean_and_fail_on_underruns_or_missing(self):
        self.write_report(**{
            "usb.dac.rate": 96000,
            "usb.dac.bits": 32,
            "usb.dac.high_speed": 1,
            "usb.dac.packets": 48000,
            "usb.dac.underruns": 0,
            "usb.dac.overflows": 0,
            "usb.dac.realigned": 0,
            "usb.dac.max_gap_us": 58,
        })
        status, result = self.check("--require-dac", "--max-dac-gap-us", "100")
        self.assertEqual(status, 0, result)
        self.assertTrue(result["ok"])

        self.write_report(**{
            "usb.dac.rate": 96000,
            "usb.dac.packets": 48000,
            "usb.dac.underruns": 3,
            "usb.dac.overflows": 0,
            "usb.dac.realigned": 0,
            "usb.dac.max_gap_us": 140,
        })
        self.assert_failed_check("usb.dac.underruns", 3, "--require-dac", "--max-dac-gap-us", "100")
        self.assert_failed_check("usb.dac.max_gap_us", 140, "--require-dac", "--max-dac-gap-us", "100")

        self.write_report()
        self.assert_failed_check("usb.dac.packets", None, "--require-dac")

    def test_dac_test_writes_reconnect_dac_streams_and_scores_report(self):
        self.write_report(**{
            "usb.dac.rate": 96000,
            "usb.dac.bits": 32,
            "usb.dac.high_speed": 1,
            "usb.dac.packets": 48000,
            "usb.dac.underruns": 0,
            "usb.dac.overflows": 0,
            "usb.dac.realigned": 0,
            "usb.dac.max_gap_us": 62,
        })
        medium = fixtures.medium("Internal storage", "ECHO", self.volume)
        host = fixtures.FakeHost(fixtures.usb_tree(fixtures.player([medium])))
        streamed = []

        def fake_streamer(rate, amplitude, connect_timeout, stream_timeout):
            streamed.append((rate, amplitude))
            return {"ok": True, "device": "Gnarlyr Echo DAC", "rate": rate, "frames": 576000}

        output = io.StringIO()
        with patch.object(echo_debug.Tool, "wait_for_return", return_value=None):
            with contextlib.redirect_stdout(output):
                status = echo_hw_check.main(
                    ["dac-test", "--rate", "96000", "--silent", "--build", BUILD, "--json"],
                    host=host,
                    streamer=fake_streamer,
                )
        result = json.loads(output.getvalue())
        self.assertEqual(status, 0, result)
        self.assertTrue(result["ok"])
        self.assertEqual(streamed, [(96000, 0.0)])
        self.assertEqual(host.ejected, [str(self.volume)])
        script_file = self.debug / echo_debug.RUN_FILE
        self.assertIn("reconnect dac", script_file.read_text(encoding="ascii"))


class ScriptAndChecklist(unittest.TestCase):
    @patch.object(echo_debug.MacHost, "usb_tree", side_effect=AssertionError("unexpected USB discovery"))
    def test_soak_script_is_validated_and_ends_by_reconnecting(self, _usb_tree):
        with patch.object(echo_debug, "prepare_script", wraps=echo_debug.prepare_script) as prepare:
            status, output = run_cli("soak-script")

        self.assertEqual(status, 0)
        prepare.assert_called_once()
        self.assertEqual(echo_debug.prepare_script(output, None), output.encode("ascii"))
        commands = echo_debug.script_lines(output)
        self.assertEqual(commands[0], "build any")
        self.assertEqual(commands[-1], "reconnect")
        self.assertIn("open nowplaying", commands)
        self.assertEqual(commands.count("play"), 2)
        self.assertIn("soak 1", commands)
        self.assertIn("set Gain=High", commands)
        self.assertIn("set DAC filter=Slow, low latency", commands)

    def test_soak_script_can_write_a_build_pinned_file(self):
        with fixtures.temporary_directory() as folder:
            path = Path(folder) / "soak.txt"
            status, output = run_cli("soak-script", "--output", str(path), "--build", BUILD)

            self.assertEqual((status, output), (0, ""))
            self.assertTrue(path.read_bytes().startswith(f"build {BUILD}\n".encode("ascii")))
            self.assertEqual(echo_debug.prepare_script(path.read_text(), None), path.read_bytes())

    def test_checklist_includes_the_manual_release_items(self):
        status, output = run_cli("checklist")

        self.assertEqual(status, 0)
        for item in ("power cycle", "3.5mm", "4.4mm", "state", "restart", "charging",
                     "usb file transfer", "usb dac", "menu", "gain", "sample rate"):
            self.assertIn(item, output.lower())

    def test_argument_validation_rejects_negative_limits_and_non_sha_builds(self):
        for arguments in (("check", "--max-underruns", "-1"), ("soak-script", "--build", BUILD + "+"),
                          ("check", "--build", "invalid")):
            with self.subTest(arguments=arguments):
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                    run_cli(*arguments)
                self.assertEqual(raised.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
