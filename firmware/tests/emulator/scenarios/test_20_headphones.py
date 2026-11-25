import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    IDENTITY,
    KEY_NEXT,
    KEY_PLAY,
    KEY_PREVIOUS,
    SETTINGS_PATH,
    SOUND_HEADPHONES_ROW,
    STARTUP_WORK_MS,
)

HEADPHONES = "/" + IDENTITY.data_directory + "/headphones.bin"
ENTRY_BYTES = 32
LONG_NAME = 0x0F
DELETED = 0xE5
PROFILE_KEYS = ((KEY_PLAY,) + (KEY_NEXT,) * SOUND_HEADPHONES_ROW + (KEY_PLAY, KEY_NEXT, KEY_PLAY)
                + (KEY_NEXT,) * 2 + (KEY_PLAY,) + (KEY_NEXT,) * 12 + (KEY_PLAY,)
                + (KEY_PREVIOUS,) * 2 + (KEY_PLAY,))


def delete_entry(session, folder, name):
    reader = session.fat()
    first = reader.find(folder)[1]
    for cluster in reader.chain(first):
        lba = reader.data_start + (cluster - 2) * reader.sectors_per_cluster
        data = bytearray(session.disk.read(lba, reader.sectors_per_cluster))
        run, long_name = [], ""
        for offset in range(0, len(data), ENTRY_BYTES):
            entry = data[offset:offset + ENTRY_BYTES]
            if entry[0] in (0, DELETED):
                run, long_name = [], ""
                continue
            if entry[11] == LONG_NAME:
                run.append(offset)
                long_name = reader.long_piece(entry) + long_name
                continue
            if (long_name or reader.short_name(entry)).lower() == name:
                for at in run + [offset]:
                    data[at] = DELETED
                session.disk.write(lba, bytes(data))
                return
            run, long_name = [], ""
    raise AssertionError(name + " not found")


class EchoImageTest(EchoImageHarness):
    def test_headphone_profiles_switch_during_playback_without_a_volume_rise(self):
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2
        rig = self.playback_rig()
        machine, session = rig.machine, rig.session
        applied = []
        targets = []
        starts = machine.trace("dmac_start_loop")
        def target_volume():
            return machine.read_memory(machine.symbols["volume_value"], 1)[0]
        def profile_change():
            applied.append((machine.reg(UC_ARM_REG_R0), machine.reg(UC_ARM_REG_R1),
                            machine.reg(UC_ARM_REG_R2) & 255))
            targets.append(target_volume())
        machine.hook_symbol("hal_audio_set_headphones", profile_change)
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(500)
        audible = rig.first_audio_after(mark)
        self.assertIsNotNone(audible)
        rig.press_many("back", 3)
        rig.press("previous")
        rig.press("play")
        rig.press("play")
        rig.press_many("next", SOUND_HEADPHONES_ROW)
        rig.press("play")
        rig.press("next")
        rig.press("play")
        rig.press_many("next", 2)
        rig.press("play")
        rig.press_many("next", 12)
        rig.press("play")
        rig.press_many("previous", 2)
        rig.press("play")
        self.assertGreater(applied[-1][0], 32700)
        self.assertLess(applied[-1][0], 33000)
        self.assertEqual(applied[-1][0], applied[-1][1])
        before = target_volume()
        saved_before = session.fat().read(SETTINGS_PATH)[13]
        rig.press("back")
        rig.press("previous")
        rig.press("play")
        rig.press("next")
        rig.press("play")
        self.assertEqual(applied[-1], (65536, 65536, 60))
        saved_after = session.fat().read(SETTINGS_PATH)[13]
        self.assertLess(saved_after, saved_before,
                        (saved_before, saved_after, applied, rig.audio.volumes[-12:]))
        self.assertGreaterEqual(targets[-1], before + 12)
        self.assertEqual(target_volume(), targets[-1])
        raw = session.fat().read("/" + IDENTITY.data_directory + "/headphones.bin")
        self.assertEqual(len(raw), 1024)
        current = [raw[offset + 28 + 7] for offset in (0, 512)]
        self.assertIn(1, current)
        self.assertEqual(rig.audio.underruns(audible), [])
        self.assertEqual(len(starts), 1)
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)


    def test_a_profile_saves_after_start_up_makes_a_missing_profile_file(self):
        from unicorn.arm_const import UC_ARM_REG_R0
        session, machine = self.started_session()
        self.assertTrue(session.fat().exists(HEADPHONES))
        delete_entry(session, "/" + IDENTITY.data_directory, "headphones.bin")
        self.assertFalse(session.fat().exists(HEADPHONES))
        session = session.reboot()
        machine = session.machine
        applied = []
        machine.hook_symbol("hal_audio_set_headphones",
                            lambda: applied.append(machine.reg(UC_ARM_REG_R0)))
        rescans = machine.trace("app_start_after_storage")
        self.assertEqual(session.run_to_home_screen(), "condition met")
        machine.run(until_ms=machine.milliseconds() + STARTUP_WORK_MS)
        self.assertTrue(session.fat().exists(HEADPHONES))
        self.assertEqual([], rescans)
        self.open_settings(machine)
        for key in PROFILE_KEYS:
            self.press(machine, key)
        self.assertLess(applied[-1], 65536, applied)
        raw = session.fat().read(HEADPHONES)
        self.assertIn(b"DSHF", raw[:4] + raw[512:516])
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
