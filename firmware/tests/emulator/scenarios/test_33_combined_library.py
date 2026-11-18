import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    IDENTITY,
    PROJECT,
    SCAN_RECORD_POLL_MS,
)

MUSIC_CHECK_PATH = "/" + IDENTITY.data_directory + "/scan.bin"
TRACK = PROJECT / "tests" / "fixtures" / "mp3" / "cbr_128_44k_stereo_tagged.mp3"


class EchoImageTest(EchoImageHarness):
    def test_card_and_internal_music_build_one_library_on_the_card(self):
        from echo_emulator.bench import LIBRARY_PATH
        track = TRACK.read_bytes()
        slot = self.with_card(files=[("CARDSONG.MP3", track, None)])
        session = self.session(files=[("INSIDE.MP3", track, None), ("CARDSONG.MP3", track, None)])
        slot.insert(session.machine)
        session.machine.stop_at("hal_system_return_scratch")
        reason = session.machine.run(until_ms=self.session_module.FRAME_LIMIT_MS)
        self.assertEqual(reason, "reached hal_system_return_scratch")
        library = slot.fat().read(LIBRARY_PATH) or b""
        self.assertEqual(library[:4], b"DSDB")
        self.assertEqual(int.from_bytes(library[12:16], "little"), 2)
        self.assertIn(b'"/INSIDE.MP3', library)
        self.assertIn(b"CARDSONG.MP3", library)
        self.assertNotIn(b'"/CARDSONG.MP3', library)
        self.assertEqual(session.read_file("INSIDE.MP3", len(track)), track)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_a_cold_start_with_nothing_changed_checks_no_music(self):
        track = TRACK.read_bytes()
        slot = self.with_card(files=[("CARDSONG.MP3", track, None)])
        first = self.session(files=[("INSIDE.MP3", track, None)])
        slot.insert(first.machine)
        record = b""
        while record[:4] != b"DSS2" and first.machine.milliseconds() < self.session_module.FRAME_LIMIT_MS:
            first.machine.run(until_ms=first.machine.milliseconds() + SCAN_RECORD_POLL_MS)
            record = slot.fat().read(MUSIC_CHECK_PATH) or b""
        self.assertEqual(record[:4], b"DSS2")
        self.assertEqual(int.from_bytes(record[44:48], "little"), 1)
        first.machine.run(until_ms=first.machine.milliseconds() + 1000)
        again = first.reboot()
        slot.insert(again.machine)
        again.machine.note("hal_storage_lend_cache")
        again.run_to_first_frame()
        again.run_to_home_screen()
        again.machine.run(until_ms=again.machine.milliseconds() + 3000)
        self.assertEqual(again.machine.reached.get("hal_storage_lend_cache", 0), 0)
        self.assert_healthy(again)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
