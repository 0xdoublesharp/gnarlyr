import contextlib
import hashlib
import io
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import firmware
import formal


def manifest():
    return formal.load_manifest()


class FakeRunner:
    def __init__(self, replies):
        self.replies = list(replies)
        self.commands = []

    def __call__(self, command, cwd, env, timeout):
        self.commands.append([str(part) for part in command])
        return self.replies.pop(0)


class ManifestTests(unittest.TestCase):
    def test_every_harness_names_files_that_exist(self):
        harnesses = manifest()["cbmc"]["harnesses"]
        for name, harness in harnesses.items():
            entry = formal.FORMAL / "cbmc" / "harnesses" / f"{harness['entry']}.c"
            self.assertTrue(entry.is_file(), name)
            for source in harness["sources"]:
                self.assertTrue((formal.SOURCE / source).is_file(), f"{name}: {source}")
            for stub in harness.get("stubs", []):
                self.assertTrue((formal.FORMAL / "cbmc" / "stubs" / stub).is_file(), f"{name}: {stub}")

    def test_every_model_names_a_spec_and_config_that_exist(self):
        for name, model in manifest()["tla"]["models"].items():
            self.assertTrue((formal.FORMAL / "tla" / model["spec"]).is_file(), name)
            self.assertTrue((formal.FORMAL / "tla" / model["config"]).is_file(), name)
            self.assertIn(model.get("expect", "passed"), ("passed", "violated"), name)

    def test_unsigned_overflow_is_not_a_global_check(self):
        self.assertNotIn("--unsigned-overflow-check", manifest()["cbmc"]["checks"])

    def test_the_canary_is_expected_to_fail(self):
        self.assertEqual(manifest()["cbmc"]["harnesses"]["canary"]["expect"], "failed")


class CommandTests(unittest.TestCase):
    def test_harnesses_compile_for_a_32_bit_target_with_unsigned_char(self):
        harness = manifest()["cbmc"]["harnesses"]["library_database"]
        command = formal.cbmc_compile_command(harness, Path("/scratch/out.goto"))
        self.assertEqual(command[0], "goto-cc")
        self.assertIn("-m32", command)
        self.assertIn("-funsigned-char", command)
        self.assertIn("-ffreestanding", command)
        self.assertIn("-DFORMAL_CARD_BYTES=160", command)
        self.assertEqual(command[-2:], ["-o", "/scratch/out.goto"])

    def test_identity_definitions_come_from_identity_json(self):
        identity = {"name": "N", "short_name": "S", "manufacturer": "M", "id": "I", "version": "2.5.1",
                    "data_directory": ".d", "update_file": "U.IMG", "playlist_start_directive": "P"}
        definitions = formal.firmware_definitions(identity)
        self.assertIn('-DFIRMWARE_DATA_DIRECTORY=".d"', definitions)
        self.assertIn('-DFIRMWARE_REVISION="2.5"', definitions)

    def test_check_command_has_global_and_per_harness_checks_with_unwinding_assertions(self):
        harness = {"unwind": 7, "checks": ["--unsigned-overflow-check"]}
        command = formal.cbmc_check_command(manifest(), harness, Path("h.goto"))
        self.assertIn("--bounds-check", command)
        self.assertIn("--unsigned-overflow-check", command)
        self.assertIn("--unwinding-assertions", command)
        self.assertEqual(command[command.index("--unwind") + 1], "7")

    def test_nondeterministic_bodies_replace_the_named_functions(self):
        steps = formal.instrument_steps({"nondet_bodies": ["a", "b"], "uninitialized": True})
        self.assertEqual([label for label, _ in steps], ["nondet", "uninitialized"])
        self.assertIn("^(a|b)$", steps[0][1])


class VerdictTests(unittest.TestCase):
    def test_a_successful_cbmc_run_passes_with_its_property_count(self):
        status, detail = formal.judge_cbmc(0, "** 0 of 12 failed (1 iterations)\nVERIFICATION SUCCESSFUL", {})
        self.assertEqual((status, detail), ("passed", "0 of 12 properties failed"))

    def test_a_failed_property_fails_unless_the_harness_expects_it(self):
        output = "[a.bounds.1] line 9 array upper bound: FAILURE\n** 1 of 9 failed (2 iterations)\nVERIFICATION FAILED"
        self.assertEqual(formal.judge_cbmc(10, output, {})[0], "failed")
        self.assertEqual(formal.judge_cbmc(10, output, {"expect": "failed", "failures": ["a.bounds.1"]})[0],
                         "expected-failure")
        self.assertEqual(formal.judge_cbmc(10, output, {"expect": "failed", "failures": ["b.other.1"]})[0],
                         "failed")

    def test_a_missing_function_body_is_an_error_not_a_pass(self):
        output = "no body for callee hal_file_read\nVERIFICATION SUCCESSFUL"
        self.assertEqual(formal.judge_cbmc(0, output, {})[0], "error")

    def test_nondet_prefixed_functions_are_not_missing_bodies(self):
        output = "no body for callee nondet_uint32\nVERIFICATION SUCCESSFUL"
        self.assertEqual(formal.judge_cbmc(0, output, {})[0], "passed")

    def test_a_timeout_is_reported_as_such(self):
        self.assertEqual(formal.judge_cbmc(None, "", {})[0], "timeout")
        self.assertEqual(formal.judge_tlc(None, "", {})[0], "timeout")

    def test_tlc_success_reports_state_counts(self):
        output = ("Model checking completed. No error has been found.\n"
                  "7253 states generated, 3581 distinct states found, 0 states left on queue.\n"
                  "The depth of the complete state graph search is 59.\n")
        self.assertEqual(formal.judge_tlc(0, output, {}), ("passed", "7253 states, 3581 distinct, depth 59"))

    def test_an_expected_violation_is_recorded_and_an_unexpected_one_fails(self):
        output = "Error: Invariant AlwaysBootable is violated.\n10 states generated, 8 distinct states found"
        model = {"expect": "violated", "property": "AlwaysBootable"}
        self.assertEqual(formal.judge_tlc(12, output, model)[0], "expected-violation")
        self.assertEqual(formal.judge_tlc(12, output, {})[0], "failed")
        temporal = "Error: Temporal properties were violated.\n"
        self.assertEqual(formal.judge_tlc(13, temporal, {"expect": "violated", "property": "temporal"})[0],
                         "expected-violation")

    def test_a_violation_of_another_property_than_the_expected_one_fails(self):
        output = "Error: Invariant TypeOK is violated.\n"
        model = {"expect": "violated", "property": "AlwaysBootable"}
        self.assertEqual(formal.judge_tlc(12, output, model)[0], "failed")

    def test_every_expected_violation_names_its_property(self):
        for name, model in manifest()["tla"]["models"].items():
            if model["expect"] == "violated":
                self.assertTrue(model.get("property"), name)

    def test_a_model_that_should_fail_but_passes_is_flagged(self):
        output = "Model checking completed. No error has been found.\n"
        self.assertEqual(formal.judge_tlc(0, output, {"expect": "violated"})[0], "unexpected-pass")


class CoverageTests(unittest.TestCase):
    def test_coverage_counts_reached_blocks_per_product_source(self):
        harness = {"sources": ["src/a.c"], "unwind": 3}
        path = formal.SOURCE / "src/a.c"
        output = (f"[f.coverage.1] file {path} line 3 function f block 1 (lines x): SATISFIED\n"
                  f"[f.coverage.2] file {path} line 9 function f block 2 (lines x): FAILED\n"
                  "[h.coverage.1] file /elsewhere/h.c line 1 function h block 1 (lines x): SATISFIED\n")
        self.assertEqual(formal.coverage_by_file(output, harness), {"src/a.c": (1, 2)})
        self.assertEqual(formal.coverage_text({"src/a.c": (1, 2)}), "a.c 1/2")

    def test_a_coverage_run_uses_cover_instead_of_the_checks(self):
        command = formal.cbmc_cover_command({"unwind": 4, "checks": ["--unsigned-overflow-check"]}, Path("h.goto"))
        self.assertIn("location", command)
        self.assertNotIn("--unwinding-assertions", command)
        self.assertNotIn("--unsigned-overflow-check", command)


class RunTests(unittest.TestCase):
    def test_a_harness_run_compiles_instruments_and_checks_in_its_own_folder(self):
        failures = "\n".join(f"[{name}] x: FAILURE" for name in manifest()["cbmc"]["harnesses"]["canary"]["failures"])
        runner = FakeRunner([(0, ""), (0, ""), (0, failures + "\n** 2 of 5 failed\nVERIFICATION FAILED")])
        with tempfile.TemporaryDirectory() as folder:
            outcome = formal.run_harness("canary", manifest(), Path(folder), {}, runner)
            self.assertTrue(outcome.log.is_file())
            self.assertEqual(Path(folder), outcome.log.parents[2])
        self.assertEqual([command[0] for command in runner.commands], ["goto-cc", "goto-instrument", "cbmc"])
        self.assertEqual(outcome.status, "expected-failure")

    def test_a_model_runs_on_copies_so_tlc_writes_nothing_into_the_repository(self):
        runner = FakeRunner([(0, "Model checking completed. No error has been found.\n")])
        with tempfile.TemporaryDirectory() as folder:
            outcome = formal.run_model("core_handoff", manifest(), Path(folder), {}, runner, jar=Path("/j.jar"))
            copied = Path(folder) / "tla" / "core_handoff" / "CoreHandoff.tla"
            self.assertTrue(copied.is_file())
        command = runner.commands[0]
        self.assertEqual(command[0], "java")
        self.assertIn("-metadir", command)
        self.assertEqual(command[-1], "CoreHandoff.tla")
        self.assertEqual(outcome.status, "passed")


class ToolDetectionTests(unittest.TestCase):
    def test_missing_tools_print_install_steps_and_exit_without_running(self):
        runner = FakeRunner([])
        error = io.StringIO()
        with mock.patch.object(formal, "require_scratch"), contextlib.redirect_stderr(error):
            code = formal.main([], runner=runner, which=lambda name: None,
                               tla_check=lambda tla, which: "java is not on PATH")
        self.assertEqual(code, formal.EXIT_MISSING_TOOLS)
        self.assertIn("brew install cbmc", error.getvalue())
        self.assertIn("--fetch-tla", error.getvalue())
        self.assertEqual(runner.commands, [])

    def test_only_the_selected_kind_needs_its_tools(self):
        problems = formal.tool_problems(manifest(), {"tla"}, which=lambda name: None,
                                        tla_check=lambda tla, which: None)
        self.assertEqual(problems, [])

    def test_a_jar_with_the_wrong_checksum_is_refused(self):
        tla = dict(manifest()["tla"])
        with tempfile.TemporaryDirectory() as folder:
            jar = Path(folder) / "tla2tools.jar"
            jar.write_bytes(b"not the release")
            problem = formal.tla_problem(tla, which=lambda name: "/usr/bin/java", jar=jar)
        self.assertIn("SHA-256", problem)

    def test_a_download_that_does_not_match_the_release_checksums_is_not_kept(self):
        tla = dict(manifest()["tla"])
        with tempfile.TemporaryDirectory() as folder:
            jar = Path(folder) / "tools" / "tla2tools.jar"
            opener = lambda url: contextlib.nullcontext(io.BytesIO(b"tampered"))
            with self.assertRaises(SystemExit):
                formal.fetch_tla(tla, jar=jar, opener=opener)
            self.assertEqual(list(jar.parent.iterdir()), [])

    def test_a_matching_download_is_saved(self):
        payload = b"release bytes"
        tla = {"url": "https://example.invalid/tla2tools.jar", "sha1": hashlib.sha1(payload).hexdigest(),
               "sha256": hashlib.sha256(payload).hexdigest()}
        with tempfile.TemporaryDirectory() as folder:
            jar = Path(folder) / "tla2tools.jar"
            formal.fetch_tla(tla, jar=jar, opener=lambda url: contextlib.nullcontext(io.BytesIO(payload)))
            self.assertEqual(jar.read_bytes(), payload)

    def test_list_needs_no_tools_and_no_scratch(self):
        output = io.StringIO()
        with mock.patch.object(formal, "require_scratch", side_effect=AssertionError), \
                contextlib.redirect_stdout(output):
            self.assertEqual(formal.main(["--list", "--tla"]), 0)
        self.assertIn("tla\tupdate_rollback", output.getvalue())
        self.assertNotIn("cbmc\t", output.getvalue())

    def test_an_unknown_harness_is_refused(self):
        with self.assertRaises(SystemExit):
            formal.main(["--list", "--harness", "nope"])


class OnDemandOnlyTests(unittest.TestCase):
    def test_firmware_formal_delegates_before_building_anything(self):
        with mock.patch.object(firmware, "environment", side_effect=AssertionError("no build environment")), \
                mock.patch("formal.main", return_value=0) as delegated:
            self.assertEqual(firmware.main(["formal", "--tla"]), 0)
        delegated.assert_called_once_with(["--tla"])

    def test_formal_checks_are_not_in_any_test_group(self):
        for pattern in firmware.GROUPS.values():
            for name in [*manifest()["cbmc"]["harnesses"], *manifest()["tla"]["models"], "formal"]:
                self.assertIsNone(re.search(pattern, name), (pattern, name))

    def test_no_cmake_file_registers_a_formal_check(self):
        skipped = {"build", "third_party", ".venv"}
        for folder, directories, files in os.walk(ROOT / "firmware"):
            directories[:] = [name for name in directories if name not in skipped]
            for name in files:
                if name == "CMakeLists.txt" or name.endswith(".cmake"):
                    text = Path(folder, name).read_text(encoding="utf-8", errors="replace")
                    self.assertIsNone(re.search(r"\bformal\b", text), Path(folder, name))

    def test_the_formal_workflow_runs_only_when_started_by_hand(self):
        workflow = (ROOT / ".github" / "workflows" / "formal.yml").read_text(encoding="utf-8")
        trigger = workflow.split("\non:", 1)[1].split("\njobs:", 1)[0]
        self.assertIn("workflow_dispatch", trigger)
        for event in ("push", "pull_request", "schedule", "workflow_call"):
            self.assertNotIn(event, trigger)

    def test_no_other_workflow_runs_the_formal_checks(self):
        for path in (ROOT / ".github" / "workflows").glob("*.yml"):
            if path.name != "formal.yml":
                self.assertNotIn("formal", path.read_text(encoding="utf-8"), path.name)


if __name__ == "__main__":
    unittest.main()
