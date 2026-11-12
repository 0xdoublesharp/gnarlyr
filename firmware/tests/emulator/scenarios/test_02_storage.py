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
    def test_a_card_inserted_after_boot_is_initialized_and_removed_without_writes(self):
        from echo_emulator.emmc import SdCard, SparseDisk
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=Fat32Volume(), module_elfs=self.module_elfs)
        disk = SparseDisk(1024 * 1024)
        Fat32Volume().write_to(disk, 0)
        card = SdCard(disk, rig.machine)
        starts = rig.machine.trace("app_start")
        rig.machine.sd_controller.card = card
        rig.machine.card_present = True
        rig.machine.run(until=lambda: bool(starts))
        rig.settle()
        self.assertTrue(any(index == 41 for _, index, _ in card.commands))
        self.assertTrue(card.reads)
        starts.clear()
        writes = len(card.writes)
        rig.machine.card_present = False
        rig.machine.sd_controller.card = None
        rig.machine.run(until=lambda: bool(starts))
        rig.settle()
        self.assertEqual(writes, len(card.writes))
        self.assert_healthy(rig.session)

    def test_eject_keeps_the_card_idle_until_physical_reinsertion(self):
        from echo_emulator.emmc import SdCard, SparseDisk
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        volume = Fat32Volume()
        disk = SparseDisk(1024 * 1024)
        volume.write_to(disk, 0)
        cards = []

        def prepare(machine):
            cards.append(SdCard(disk, machine))
            machine.sd_controller.card = cards[0]
            machine.card_present = True

        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=Fat32Volume(), module_elfs=self.module_elfs, prepare=prepare)
        notices = rig.machine.collect_notices()
        rig.press("previous")
        rig.press("play")
        rig.press_many("previous", 6)
        rig.press("play")
        rig.press("play")
        self.assertIn(("Eject card", "Safe to remove card"), notices)
        before = len(cards[0].commands)
        rig.run_for(1000)
        self.assertEqual(before, len(cards[0].commands))
        rig.machine.card_present = False
        rig.run_for(100)
        rig.machine.card_present = True
        rig.machine.run(until=lambda: len(cards[0].commands) > before)
        self.assertGreater(len(cards[0].commands), before)
        self.assert_healthy(rig.session)

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
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()
                          if call["module"] == number], [(number, True)])
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
