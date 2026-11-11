import sys
import struct
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import echo_image_harness as image_tests

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

    def test_arm_rescan_keeps_legacy_half_millisecond_durations(self):
        def wave(frames):
            pcm = bytes(frames * 2)
            return struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                               16, 1, 1, 48000, 96000, 2, 16, b"data", len(pcm)) + pcm
        files = [("EVEN.WAV", wave(24024), None), ("ODD.WAV", wave(24072), None),
                 ("WHOLE.WAV", wave(48000), None)]
        session = self.session(files=files)
        session.machine.stop_at("hal_system_return_scratch")
        self.assertEqual(session.machine.run(until_ms=self.session_module.FRAME_LIMIT_MS),
                         "reached hal_system_return_scratch")
        database = session.fat().read("/" + image_tests.IDENTITY.data_directory + "/library.db")
        offset, count = struct.unpack_from("<II", database, 8)
        self.assertEqual(count, 3)
        durations = [struct.unpack_from("<I", database, offset + index * 32 + 16)[0]
                     for index in range(count)]
        self.assertEqual(sorted(durations), [500, 501, 1000])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)



if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
