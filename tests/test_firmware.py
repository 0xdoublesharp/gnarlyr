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


class CompilerCacheTests(unittest.TestCase):
    def launcher(self, setting, installed):
        env = {"PATH": "/toolchain/bin"}
        if setting is not None:
            env["ECHO_COMPILER_CACHE"] = setting
        with mock.patch.object(firmware.shutil, "which", return_value=installed):
            with mock.patch.object(firmware, "run") as run:
                firmware.configure("release", env, Path("/python"))
        return run.call_args.args[0][-1]

    def test_cache_is_enabled_by_default_when_installed(self):
        self.assertEqual(self.launcher(None, "/bin/sccache"),
                         "-DCMAKE_C_COMPILER_LAUNCHER=/bin/sccache")

    def test_cache_can_be_explicitly_disabled(self):
        self.assertEqual(self.launcher("0", "/bin/sccache"),
                         "-DCMAKE_C_COMPILER_LAUNCHER=")

    def test_cache_can_be_explicitly_enabled(self):
        self.assertEqual(self.launcher("1", "/bin/sccache"),
                         "-DCMAKE_C_COMPILER_LAUNCHER=/bin/sccache")

    def test_missing_cache_does_not_prevent_building(self):
        self.assertEqual(self.launcher(None, None), "-DCMAKE_C_COMPILER_LAUNCHER=")


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


class FuzzCommandTests(unittest.TestCase):
    def test_cue_sheet_target_has_boundary_seeds(self):
        self.assertEqual(firmware.FUZZ_TARGETS["cue_sheet"], 65536)
        corpus = b"\n".join(firmware.FUZZ_SEEDS["cue_sheet"])
        for boundary in (b"TRACK 00", b"TRACK 100", b"00:60:00", b"00:00:75"):
            self.assertIn(boundary, corpus)

    def test_settings_codec_target_has_bounded_structured_seeds(self):
        self.assertEqual(firmware.FUZZ_TARGETS["settings_codec"], 128)
        self.assertEqual(
            [seed[0] for seed in firmware.FUZZ_SEEDS["settings_codec"]],
            [0, 1, 2, 3],
        )
        self.assertEqual(
            [len(seed) for seed in firmware.FUZZ_SEEDS["settings_codec"]],
            [128, 33, 65, 33],
        )

    def test_high_value_parser_and_store_targets_are_registered(self):
        self.assertEqual(firmware.FUZZ_TARGETS["library_database"], 65536)
        self.assertEqual(firmware.FUZZ_TARGETS["mp4_demux"], 262144)
        self.assertEqual(firmware.FUZZ_TARGETS["store"], 4096)

    def test_discovery_state_machine_target_is_bounded_and_seeded(self):
        self.assertEqual(firmware.FUZZ_TARGETS["discovery_screen"], 256)
        self.assertEqual(len(firmware.FUZZ_SEEDS["discovery_screen"]), 3)
        self.assertTrue(all(len(seed) <= 256 for seed in firmware.FUZZ_SEEDS["discovery_screen"]))

    def test_personal_tags_state_machine_target_is_bounded_and_seeded(self):
        self.assertEqual(firmware.FUZZ_TARGETS["personal_tags"], 320)
        self.assertEqual(len(firmware.FUZZ_SEEDS["personal_tags"]), 3)
        self.assertTrue(all(len(seed) <= 320 for seed in firmware.FUZZ_SEEDS["personal_tags"]))

    def test_browse_state_machine_target_is_bounded_and_seeded(self):
        self.assertEqual(firmware.FUZZ_TARGETS["browse_screen"], 256)
        self.assertEqual(len(firmware.FUZZ_SEEDS["browse_screen"]), 3)
        self.assertTrue(all(len(seed) <= 256 for seed in firmware.FUZZ_SEEDS["browse_screen"]))

    def test_app_navigation_state_machine_target_is_bounded_and_seeded(self):
        self.assertEqual(firmware.FUZZ_TARGETS["app_navigation"], 128)
        self.assertEqual(len(firmware.FUZZ_SEEDS["app_navigation"]), 3)
        self.assertTrue(all(len(seed) <= 128 for seed in firmware.FUZZ_SEEDS["app_navigation"]))

    def test_library_builder_target_is_bounded_and_seeded(self):
        self.assertEqual(firmware.FUZZ_TARGETS["library_builder"], 512)
        seeds = firmware.FUZZ_SEEDS["library_builder"]
        self.assertEqual(len(seeds), 3 + 9 * 255)
        self.assertTrue(all(len(seed) <= 512 for seed in seeds))
        self.assertTrue(any(seed[0] == 1 for seed in seeds[3:]))
        self.assertTrue(any(seed[3] == 255 and seed[6] == 8 for seed in seeds[3:]))

    def test_cover_image_target_has_canonical_png_fixtures(self):
        self.assertEqual(firmware.FUZZ_TARGETS["cover_image"], 1048576)
        fixtures = {path.name for path in firmware.FUZZ_FIXTURES["cover_image"]}
        self.assertEqual(fixtures, {"home.png", "cover-flow.png", "home-280c6baaadf6.png"})
        self.assertEqual(len(firmware.COVER_IMAGE_JPEG_FIXTURES), 3)

    def test_ui_and_queue_targets_are_bounded_and_seeded(self):
        for name in ("placeholder_art", "play_queue", "audio_engine", "playback_views"):
            self.assertEqual(firmware.FUZZ_TARGETS[name], 256)
            self.assertTrue(firmware.FUZZ_SEEDS[name])
            self.assertTrue(all(len(seed) <= 256 for seed in firmware.FUZZ_SEEDS[name]))

        self.assertEqual(firmware.FUZZ_TARGETS["card_format"], 64)
        self.assertTrue(firmware.FUZZ_SEEDS["card_format"])
        self.assertTrue(all(len(seed) <= 64 for seed in firmware.FUZZ_SEEDS["card_format"]))

        self.assertEqual(firmware.FUZZ_TARGETS["ogg_track"], 196608)
        self.assertGreaterEqual(len(firmware.FUZZ_FIXTURES["ogg_track"]), 10)
        self.assertTrue(all(path.exists() for path in firmware.FUZZ_FIXTURES["ogg_track"]))

    def test_decoder_targets_are_bounded_and_have_canonical_inputs(self):
        self.assertEqual(firmware.FUZZ_TARGETS["mp3_probe"], 262144)
        self.assertEqual(firmware.FUZZ_TARGETS["opus_decode"], 2048)
        self.assertEqual(len(firmware.FUZZ_FIXTURES["mp3_probe"]), 3)

    def test_fat_target_has_each_supported_volume_fixture(self):
        self.assertEqual(firmware.FUZZ_TARGETS["fat_volume"], 400000)
        fixtures = {path.name for path in firmware.FUZZ_FIXTURES["fat_volume"]}
        self.assertEqual(
            fixtures,
            {"fat16_superfloppy.sparse", "fat32_mbr.sparse", "exfat_mbr.sparse"},
        )

    def test_binary_parser_targets_have_canonical_fixture_seeds(self):
        library = {path.name for path in firmware.FUZZ_FIXTURES["library_database"]}
        mp4 = {path.name for path in firmware.FUZZ_FIXTURES["mp4_demux"]}
        self.assertEqual(
            library,
            {"small_library.db", "cue_music.db", "library.db", "with_playlists.db"},
        )
        self.assertIn("01 Lead.m4a", mp4)
        self.assertIn("02 Tail.m4a", mp4)
        self.assertIn("02 Deep.m4a", mp4)

    def test_fuzz_cli_selects_a_bounded_reproducible_campaign(self):
        with mock.patch.object(
            sys,
            "argv",
            ["firmware.py", "fuzz", "--target", "track_tags", "--runs", "250", "--seed", "7"],
        ):
            _, args = firmware.arguments()
        self.assertEqual(args.target, ["track_tags"])
        self.assertEqual((args.runs, args.seed), (250, 7))

    def test_fuzz_command_keeps_corpus_and_artifacts_explicit(self):
        command = firmware.fuzz_command(
            Path("/build/fuzz_track_tags"),
            Path("/scratch/corpus"),
            Path("/scratch/artifacts"),
            runs=250,
            seed=7,
            max_length=65536,
        )
        self.assertEqual(command[0], Path("/build/fuzz_track_tags"))
        self.assertIn("-runs=250", command)
        self.assertIn("-seed=7", command)
        self.assertIn("-max_len=65536", command)
        self.assertIn("-artifact_prefix=/scratch/artifacts/", command)
        self.assertEqual(command[-1], Path("/scratch/corpus"))

    def test_fuzz_options_cannot_be_silently_ignored_by_tests(self):
        with mock.patch.object(sys, "argv", ["firmware.py", "test", "--runs", "10"]):
            with mock.patch("sys.stderr"), self.assertRaises(SystemExit) as error:
                firmware.arguments()
        self.assertEqual(error.exception.code, 2)

    def test_fuzz_seed_must_select_a_reproducible_sequence(self):
        with mock.patch.object(sys, "argv", ["firmware.py", "fuzz", "--seed", "0"]):
            with mock.patch("sys.stderr"), self.assertRaises(SystemExit) as error:
                firmware.arguments()
        self.assertEqual(error.exception.code, 2)

    def test_canonical_seed_is_refreshed_without_replacing_generated_entries(self):
        path = mock.Mock()
        path.exists.return_value = True
        path.read_bytes.return_value = b"old"
        firmware.write_seed(path, b"new")
        path.write_bytes.assert_called_once_with(b"new")

    def test_fuzz_configuration_pins_the_clang_selected_by_the_wrapper(self):
        with mock.patch.object(firmware.shutil, "which", return_value="/toolchain/bin/clang"):
            with mock.patch.object(firmware, "run") as run:
                firmware.configure("fuzz", {"PATH": "/toolchain/bin"}, Path("/python"))
        self.assertIn("-DCMAKE_C_COMPILER=/toolchain/bin/clang", run.call_args.args[0])


if __name__ == "__main__":
    unittest.main()
