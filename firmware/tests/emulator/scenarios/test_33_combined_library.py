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
RESCAN_TIMES_PATH = "/" + IDENTITY.data_directory + "/rescan_times.bin"
SECTOR = 512
INTERNAL_LUN = 0
ENTRY_BYTES = 32
WRITE_TIME = 22
SETTLE_MS = 300
CONFIGURE_LIMIT_MS = 2000
AFTER_SESSION_MS = 6000
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
        self.assertEqual(internal().read(MUSIC_GENERATION_PATH)[:4], (2).to_bytes(4, "little"))
        self.assert_only_the_user_volume_changed(rig.session)
        self.assert_healthy(rig.session)

    def combined_player(self, internal=(("INSIDE.MP3", None),), card_in=True):
        track = TRACK.read_bytes()
        slot = self.with_card(files=[("CARDSONG.MP3", track, None)])
        session = self.session(files=[(name, track, None) for name, _ in internal])
        if card_in:
            slot.insert(session.machine)
        read = (lambda: slot.fat().read(MUSIC_CHECK_PATH)) if card_in else \
            (lambda: session.fat().read(MUSIC_CHECK_PATH))
        self.assertEqual(self.wait_for_record(session.machine, read)[:4], b"DSS2")
        session.machine.run(until_ms=session.machine.milliseconds() + 1500)
        return slot, session

    def test_a_usb_session_that_only_rewrites_internal_storage_checks_without_a_rebuild(self):
        slot, session = self.combined_player()
        times = slot.fat().read(RESCAN_TIMES_PATH)
        record = slot.fat().read(MUSIC_CHECK_PATH)[:52]
        host_rewrites_internal_entry(self, session, b"INSIDE  MP3", changed=False)
        self.assertEqual(session.fat().read(MUSIC_GENERATION_PATH)[:4], (1).to_bytes(4, "little"))
        after = slot.fat().read(MUSIC_CHECK_PATH)[:52]
        self.assertEqual(after[32:48], record[32:48])
        self.assertEqual(after[48:52], (1).to_bytes(4, "little"))
        self.assertEqual(slot.fat().read(RESCAN_TIMES_PATH), times)
        self.assert_healthy(session)

    def test_a_usb_session_that_changes_internal_music_rebuilds_the_card_library(self):
        slot, session = self.combined_player()
        times = slot.fat().read(RESCAN_TIMES_PATH)
        record = slot.fat().read(MUSIC_CHECK_PATH)[:52]
        host_rewrites_internal_entry(self, session, b"INSIDE  MP3", changed=True)
        after = slot.fat().read(MUSIC_CHECK_PATH)[:52]
        self.assertNotEqual(after[32:44], record[32:44])
        self.assertNotEqual(slot.fat().read(RESCAN_TIMES_PATH), times)
        self.assert_healthy(session)


    def change_internal_music_without_the_card(self, slot, session):
        machine = session.machine
        slot.remove(machine)
        self.assertEqual(self.wait_for_record(machine, lambda: session.fat().read(MUSIC_CHECK_PATH))[:4],
                         b"DSS2")
        machine.run(until_ms=machine.milliseconds() + 1500)
        host_rewrites_internal_entry(self, session, b"INSIDE  MP3", changed=True)
        self.assertEqual(session.fat().read(MUSIC_GENERATION_PATH)[:4], (1).to_bytes(4, "little"))

    def test_a_card_reinserted_after_internal_music_changed_rebuilds_its_library(self):
        slot, session = self.combined_player()
        times = slot.fat().read(RESCAN_TIMES_PATH)
        self.change_internal_music_without_the_card(slot, session)
        slot.insert(session.machine)
        session.machine.run(until_ms=session.machine.milliseconds() + AFTER_SESSION_MS)
        self.assertNotEqual(slot.fat().read(RESCAN_TIMES_PATH), times)
        self.assertEqual(slot.fat().read(MUSIC_CHECK_PATH)[48:52], (1).to_bytes(4, "little"))
        self.assert_healthy(session)

    def test_a_card_in_at_power_on_after_internal_music_changed_rebuilds_its_library(self):
        slot, session = self.combined_player()
        times = slot.fat().read(RESCAN_TIMES_PATH)
        self.change_internal_music_without_the_card(slot, session)
        again = session.reboot()
        slot.insert(again.machine)
        again.run_to_first_frame()
        again.machine.run(until_ms=again.machine.milliseconds() + AFTER_SESSION_MS * 2)
        self.assertNotEqual(slot.fat().read(RESCAN_TIMES_PATH), times)
        self.assertEqual(slot.fat().read(MUSIC_CHECK_PATH)[48:52], (1).to_bytes(4, "little"))
        self.assert_healthy(again)

    def test_removing_the_card_while_an_internal_song_plays_stops_and_switches_libraries(self):
        from echo_emulator.bench import LIBRARY_PATH
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        volume = Fat32Volume()
        for name in ("A.wav", "B.wav"):
            volume.add_file(name, long_wav())
        slot = self.with_card(files=[("C.wav", long_wav(), None)])
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs, prepare=slot.insert)
        machine = rig.machine
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(1500)
        self.assertIsNotNone(rig.first_audio_after(mark))
        removed = machine.milliseconds()
        slot.remove(machine)
        record = self.wait_for_record(machine, lambda: rig.session.fat().read(MUSIC_CHECK_PATH))
        self.assertEqual(int.from_bytes(record[44:48], "little"), 0)
        rig.run_for(1000)
        self.assertIsNone(rig.first_audio_after(removed + 500))
        internal = rig.session.fat().read(LIBRARY_PATH) or b""
        self.assertEqual(int.from_bytes(internal[12:16], "little"), 2)
        self.assertNotIn(b'"/', internal)
        self.assert_healthy(rig.session)

    def blank_card_rig(self):
        from echo_emulator.emmc import SdCard, SparseDisk
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        card = SparseDisk(2 * 2 * 1024 * 1024)
        inserted = []

        def prepare(machine):
            inserted.append(SdCard(card, machine))
            machine.sd_controller.card = inserted[0]
            machine.card_present = True

        volume = Fat32Volume()
        volume.add_file("A.wav", long_wav())
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs, prepare=prepare)
        return rig, card, inserted

    def test_a_card_unreadable_at_boot_leaves_the_internal_library_alone(self):
        from echo_emulator.bench import LIBRARY_PATH
        rig, card, inserted = self.blank_card_rig()
        record = self.wait_for_record(rig.machine, lambda: rig.session.fat().read(MUSIC_CHECK_PATH))
        self.assertEqual(int.from_bytes(record[44:48], "little"), 0)
        internal = rig.session.fat().read(LIBRARY_PATH) or b""
        self.assertEqual(int.from_bytes(internal[12:16], "little"), 1)
        self.assertNotIn(b'"/', internal)
        self.assertEqual([], inserted[0].writes)
        self.assert_healthy(rig.session)

    def test_formatting_a_card_builds_its_library_from_internal_music(self):
        import struct
        from echo_emulator.fat_reader import FatReader
        rig, card, inserted = self.blank_card_rig()
        rig.press("previous")
        rig.press("play")
        rig.press_many("previous", 6)
        rig.press("play")
        rig.press("play")
        rig.press("next")
        rig.press("play")
        rig.machine.run(until=lambda: card.read(0, 1)[510:512] == b"\x55\xaa")
        reader = FatReader(card, struct.unpack_from("<I", card.read(0, 1), 454)[0])
        library = b""
        deadline = rig.machine.milliseconds() + self.session_module.FRAME_LIMIT_MS * 2
        while library[:4] != b"DSDB" or int.from_bytes(library[12:16], "little") == 0:
            if rig.machine.milliseconds() > deadline:
                break
            rig.run_for(SCAN_RECORD_POLL_MS * 5)
            library = reader.read(f"/{IDENTITY.data_directory}/library.db") or b""
        self.assertEqual(int.from_bytes(library[12:16], "little"), 1)
        self.assertIn(b'"/A.wav', library)
        self.assertTrue(rig.session.fat().exists("/A.wav"))
        self.assert_healthy(rig.session)


def long_wav():
    import struct
    pcm = struct.pack("<8h", 0, 2828, 4000, 2828, 0, -2828, -4000, -2828) * 10000
    return struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                       16, 1, 1, 8000, 16000, 2, 16, b"data", len(pcm)) + pcm


def entry_sector(session, short_name):
    reader = session.fat()
    first = reader.data_start + (reader.root - 2) * reader.sectors_per_cluster
    for lba in range(first, first + reader.sectors_per_cluster):
        sector = bytearray(session.disk.read(lba, 1))
        at = sector.find(short_name)
        if at >= 0 and at % ENTRY_BYTES == 0:
            return lba - session.layout["volume_start"], sector, at
    raise AssertionError("no directory entry for %r" % short_name)


def host_rewrites_internal_entry(test, session, short_name, changed):
    from echo_emulator.usb_storage_host import ScsiHost, StorageHost
    machine = session.machine
    lba, sector, at = entry_sector(session, short_name)
    if changed:
        sector[at + WRITE_TIME] ^= 0x20
    finished = [False]
    host = StorageHost(machine, SECTOR, 400)
    machine.usb.host = host
    machine.host_stays = True
    machine.stub_symbol("msc_finished", lambda machine: int(finished[0]))
    machine.stub_symbol("usb_activity_quiet", lambda machine: 1)
    machine.vbus = True
    test.assertEqual(machine.run(until=lambda: machine.usb_reset_ack,
                                 until_ms=machine.milliseconds() + test.session_module.FRAME_LIMIT_MS),
                     "condition met")
    host.enumerate()
    test.assertEqual(machine.run(until=host.configured,
                                 until_ms=machine.milliseconds() + CONFIGURE_LIMIT_MS), "condition met")
    machine.run(until_ms=machine.milliseconds() + SETTLE_MS)
    scsi = ScsiHost(machine, host)
    test.assertTrue(scsi.until_ready(INTERNAL_LUN))
    scsi.write(INTERNAL_LUN, lba, bytes(sector))
    finished[0] = True
    machine.run(until_ms=machine.milliseconds() + AFTER_SESSION_MS)
    machine.vbus = False
    machine.run(until_ms=machine.milliseconds() + 1000)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
