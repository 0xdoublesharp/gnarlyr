import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    PROJECT,
    SCAN_RECORD_POLL_MS,
)


class EchoImageTest(EchoImageHarness):
    def test_without_an_sd_card_music_on_the_internal_volume_is_scanned(self):
        track = (PROJECT / "tests" / "fixtures" / "mp3" /
                 "cbr_128_44k_stereo_tagged.mp3").read_bytes()
        session = self.session(files=[("TRACK01.MP3", track, None)])
        session.machine.stop_at("hal_system_return_scratch")
        reason = session.machine.run(until_ms=self.session_module.FRAME_LIMIT_MS)
        self.assertEqual(reason, "reached hal_system_return_scratch")
        self.assertEqual(self.built_library_tracks(session), 1)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.assertEqual(session.read_file("TRACK01.MP3", len(track)), track)


    def test_the_scan_record_names_the_version_and_the_build_tag(self):
        from echo_emulator.modules import ModuleWatch
        track = (PROJECT / "tests" / "fixtures" / "mp3" /
                 "cbr_128_44k_stereo_tagged.mp3").read_bytes()
        session = self.session(files=[("TRACK01.MP3", track, None)])
        machine = session.machine
        watch = ModuleWatch(session)
        number, _ = self.module(machine, "library")
        deadline = self.session_module.FRAME_LIMIT_MS
        while self.recorded_version(session) is None and machine.milliseconds() < deadline:
            machine.run(until_ms=machine.milliseconds() + SCAN_RECORD_POLL_MS)
        build_tag = self.image[0x30:0x38].rstrip(b"\0").decode("ascii")
        self.assertEqual(self.recorded_version(session), "1.0.0-" + build_tag)
        self.assertGreaterEqual(self.recorded_entries(session), 1)
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()],
                         [(number, True)])
        self.assert_healthy(session)
        print("\n[music walk after the first scan] " + watch.describe(watch.loads()[0]))



if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
