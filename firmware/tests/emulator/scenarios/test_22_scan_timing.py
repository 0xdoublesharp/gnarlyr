import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    CONFIG,
    EchoImageHarness,
    main,
    tempfile,
)

TRACKS = 24
SEED = "scenario"


class EchoImageTest(EchoImageHarness):
    def test_scan_timing_accounts_for_a_generated_library_scan(self):
        from echo_emulator import bench, music_library
        artifacts = bench.Artifacts(Path(CONFIG["elf"]).parents[2])
        with tempfile.TemporaryDirectory() as directory:
            volume = music_library.cached(TRACKS, SEED, directory, log=lambda text: None)
            summary = volume.manifest["summary"]
            session, run = bench.first_scan(artifacts, volume, print)
            result = run.result()
            self.assertEqual(result["counts"]["library_tracks"], summary["planned_tracks"])
            with_art = summary["albums"] - summary["albums_without_art"]
            self.assertEqual(result["decoded_covers"], with_art)
            self.assertEqual(result["counts"]["stored_covers"], with_art)
            self.assertGreater(result["emulated_ms"], 0)
            phases = result["profile"]["phases"]
            tags = [phase for phase in phases if phase["stage"] == "scan" and phase["phase"] == "tags"]
            self.assertGreater(sum(phase["read_commands"] for phase in tags), 0)
            decode = [phase for phase in phases if phase["stage"] == "covers" and phase["phase"] == "decode"]
            self.assertGreater(sum(phase["instructions"] for phase in decode), 0)
            writes = [phase for phase in phases if phase["stage"] == "covers" and phase["kind"] == "write"]
            self.assertGreater(sum(phase["write_commands"] for phase in writes), 0)
            total = sum(phase["us"] for phase in phases)
            self.assertAlmostEqual(total / 1000, result["emulated_ms"], delta=1)
            self.assertIsNotNone(result["fingerprint_walk_after_scan"])
            self.assert_healthy(session)
            print("\n[scan timing] %d tracks, %.1f ms emulated, %d covers decoded" % (
                result["counts"]["library_tracks"], result["emulated_ms"], result["decoded_covers"]))


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
