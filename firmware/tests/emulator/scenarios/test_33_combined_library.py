import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from echo_image_harness import (
    EchoImageHarness,
    main,
    IDENTITY,
    PROJECT,
    SCAN_RECORD_POLL_MS,
)

MUSIC_CHECK_PATH = "/" + IDENTITY.data_directory + "/scan.bin"
MUSIC_GENERATION_PATH = "/" + IDENTITY.data_directory + "/music_gen.bin"
FAVORITE_ROW = 2
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

    def wait_for_record(self, machine, read):
        record = b""
        deadline = machine.milliseconds() + self.session_module.FRAME_LIMIT_MS
        while record[:4] != b"DSS2" and machine.milliseconds() < deadline:
            machine.run(until_ms=machine.milliseconds() + SCAN_RECORD_POLL_MS)
            record = read() or b""
        return record

    def test_removing_the_card_uses_the_internal_library_and_reinserting_keeps_the_combined_one(self):
        from echo_emulator.bench import LIBRARY_PATH
        track = TRACK.read_bytes()
        slot = self.with_card(files=[("CARDSONG.MP3", track, None)])
        session = self.session(files=[("INSIDE.MP3", track, None)])
        machine = session.machine
        slot.insert(machine)
        self.assertEqual(self.wait_for_record(machine, lambda: slot.fat().read(MUSIC_CHECK_PATH))[:4],
                         b"DSS2")
        machine.run(until_ms=machine.milliseconds() + 1000)
        combined = slot.fat().read(LIBRARY_PATH)
        slot.remove(machine)
        record = self.wait_for_record(machine, lambda: session.fat().read(MUSIC_CHECK_PATH))
        self.assertEqual(int.from_bytes(record[44:48], "little"), 0)
        internal = session.fat().read(LIBRARY_PATH) or b""
        self.assertEqual(int.from_bytes(internal[12:16], "little"), 1)
        self.assertIn(b"INSIDE.MP3", internal)
        self.assertNotIn(b'"/', internal)
        machine.run(until_ms=machine.milliseconds() + 1000)
        slot.insert(machine)
        machine.run(until_ms=machine.milliseconds() + 4000)
        self.assertEqual(slot.fat().read(LIBRARY_PATH), combined)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_an_internal_song_is_favorited_and_deleted_on_internal_storage_with_a_card_in(self):
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        from test_23_hide_delete import DELETE_ROW, HOLD_MS, short_wav
        volume = Fat32Volume()
        for name in ("A.wav", "B.wav"):
            volume.add_file(name, short_wav())
        slot = self.with_card(files=[("C.wav", short_wav(), None)])
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs, prepare=slot.insert)
        internal = rig.session.fat
        favorites = "/Favorite Tracks.m3u"

        def song_action(row):
            rig.press("play", hold=HOLD_MS)
            rig.press_many("next", row)
            rig.press("play")

        rig.open_songs()
        song_action(FAVORITE_ROW)
        self.assertTrue((internal().read(favorites) or b"").startswith(b"#EXTM3U\nA.wav\n"))
        self.assertFalse(slot.fat().exists(favorites))
        rig.press("next")
        song_action(DELETE_ROW)
        rig.press("next")
        rig.press("play")
        self.assertFalse(internal().exists("/B.wav"))
        self.assertTrue(internal().exists("/A.wav"))
        self.assertTrue(slot.fat().exists("/C.wav"))
        self.assertEqual(internal().read(MUSIC_GENERATION_PATH)[:4], (1).to_bytes(4, "little"))
        self.assert_only_the_user_volume_changed(rig.session)
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
