import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import firmware


class FocusedBuildTests(unittest.TestCase):
    def setUp(self):
        self.binary = Path("/build/tests/test_player")
        self.helper = Path("/build/tests/cover_decode_tool")
        self.artifacts = {self.binary: "test_player", self.helper: "cover_decode_tool"}

    def test_native_test_builds_its_executable_even_when_the_test_has_another_name(self):
        tests = [{"name": "playback_behavior", "command": [str(self.binary)]}]
        self.assertEqual(firmware.build_targets(tests, self.artifacts), ["test_player"])

    def test_python_check_builds_the_helper_passed_as_an_argument(self):
        tests = [{"name": "cover_check", "command": [sys.executable, "check.py", "--tool", str(self.helper)]}]
        self.assertEqual(firmware.build_targets(tests, self.artifacts), ["cover_decode_tool"])

    def test_a_cold_unit_test_can_be_built_before_ctest_knows_its_command(self):
        self.assertEqual(firmware.build_targets([{"name": "test_player"}], self.artifacts), ["test_player"])

    def test_an_unknown_cold_command_falls_back_to_the_complete_build(self):
        self.assertIsNone(firmware.build_targets([{"name": "new_sim_journey"}], self.artifacts))

    def test_a_script_only_check_does_not_build_unrelated_executables(self):
        tests = [{"name": "format", "command": ["/usr/bin/clang-format", "--dry-run"]}]
        self.assertEqual(firmware.build_targets(tests, self.artifacts), [])

    def test_a_fixture_and_its_consumer_build_all_referenced_helpers_once(self):
        tests = [{"name": "test_player", "command": [str(self.binary)]},
                 {"name": "check_player", "command": [sys.executable, "check.py", str(self.binary), str(self.helper)]}]
        self.assertEqual(firmware.build_targets(tests, self.artifacts), ["cover_decode_tool", "test_player"])

    def test_an_empty_selection_fails_instead_of_reporting_a_successful_check(self):
        with self.assertRaisesRegex(ValueError, "No tests"):
            firmware.build_targets([], self.artifacts)


class SelectionTests(unittest.TestCase):
    def test_ctest_fixture_setup_is_kept_in_the_selected_plan(self):
        payload = '{"tests": [{"name": "device_build"}, {"name": "boot", "command": ["/usr/bin/python3", "boot.py"]}]}'
        with mock.patch.object(firmware.subprocess, "check_output", return_value=payload) as command:
            tests = firmware.selected_tests("^boot$", {})
        self.assertEqual([test["name"] for test in tests], ["device_build", "boot"])
        self.assertIn("--show-only=json-v1", command.call_args.args[0])
        self.assertEqual(command.call_args.args[0][-2:], ["-R", "^boot$"])

    def test_a_misspelled_test_is_rejected_before_any_build(self):
        with mock.patch.object(firmware.subprocess, "check_output", return_value='{"tests": []}'):
            with self.assertRaisesRegex(ValueError, "No tests"):
                firmware.selected_tests("^typo$", {})

    def test_script_only_selection_does_not_invoke_the_build(self):
        with mock.patch.object(firmware, "run") as run:
            firmware.compile_targets("dev", {}, [])
        run.assert_not_called()

    def test_cli_separates_build_and_test_parallelism(self):
        with mock.patch.object(sys, "argv", ["firmware.py", "test", "--build-jobs", "2", "--test-jobs", "6"]):
            _, args = firmware.arguments()
        self.assertEqual((args.build_jobs, args.test_jobs), (2, 6))

    def test_test_selection_cannot_be_silently_ignored_by_a_build_action(self):
        with mock.patch.object(sys, "argv", ["firmware.py", "build", "--regex", "test_player"]):
            with mock.patch("sys.stderr"), self.assertRaises(SystemExit) as error:
                firmware.arguments()
        self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
