import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import firmware_tasks


class GenerationTests(unittest.TestCase):
    def test_generation_and_check_use_the_same_complete_plan(self):
        write = firmware_tasks.generators(Path("/source"), Path("/python"), False)
        check = firmware_tasks.generators(Path("/source"), Path("/python"), True)
        self.assertEqual(check, [command + ["--check"] for command in write])
        scripts = {Path(command[1]).name for command in write}
        self.assertTrue({"lang_pack.py", "glyph_pack.py", "font_build.py", "fold_table.py",
                         "audio_resample_table.py", "replay_gain_table.py"} <= scripts)
        self.assertTrue(any("--export-catalogue" in command for command in write))

    def test_check_runs_every_generator_and_reports_all_failures(self):
        with mock.patch.object(firmware_tasks.subprocess, "run") as run:
            run.side_effect = [mock.Mock(returncode=1), mock.Mock(returncode=0)]
            with mock.patch.object(firmware_tasks, "generators", return_value=[["python", "a"], ["python", "b"]]):
                with self.assertRaisesRegex(ValueError, "firmware.py regen"):
                    firmware_tasks.regenerate(Path("/source"), {}, Path("/python"), True)
        self.assertEqual(run.call_count, 2)


class SizeTests(unittest.TestCase):
    def test_floor_is_enforced_even_when_the_image_fits(self):
        self.assertFalse(firmware_tasks.Size("release", "resident code", 95, 100, 8).ok)
        self.assertTrue(firmware_tasks.Size("release", "resident code", 92, 100, 8).ok)

    def test_slot_overflow_is_rejected(self):
        self.assertFalse(firmware_tasks.Size("device", "extension", 101, 100).ok)

    def test_size_table_checks_every_row_before_returning_failure(self):
        rows = [firmware_tasks.Size("release", "resident code", 95, 100, 8),
                firmware_tasks.Size("device", "module extension", 90, 100)]
        fake_report = mock.Mock()
        fake_report.format_table.return_value = ["table"]
        with mock.patch.object(firmware_tasks, "size_rows", side_effect=[[rows[0]], [rows[1]]]):
            with mock.patch("importlib.import_module", return_value=fake_report):
                with self.assertRaisesRegex(ValueError, "floor or slot"):
                    firmware_tasks.print_sizes(Path("/source"))
        self.assertEqual(len(fake_report.format_table.call_args.args[0]), 2)


class CliTests(unittest.TestCase):
    def test_new_commands_accept_their_own_flags(self):
        import firmware

        for action, flag in (("regen", "--check"), ("sizes", "--existing"),
                             ("install-device", "--dry-run"), ("install-device", "--owner-approved")):
            with mock.patch.object(sys, "argv", ["firmware.py", action, flag, "--jobs", "2"]):
                _, args = firmware.arguments()
                self.assertEqual(args.action, action)

    def test_new_flags_are_rejected_when_they_would_be_ignored(self):
        import firmware

        for flag in ("--check", "--existing", "--dry-run", "--owner-approved"):
            with mock.patch.object(sys, "argv", ["firmware.py", "build", flag]):
                with mock.patch("sys.stderr"), self.assertRaises(SystemExit):
                    firmware.arguments()


if __name__ == "__main__":
    unittest.main()
