import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, HOME_LISTENING_ROW, IDENTITY, main


class EchoImageTest(EchoImageHarness):
    def test_a_two_sided_tape_stops_flips_and_resumes_side_b_after_restart(self):
        from echo_emulator.audio import AudioPath
        from echo_emulator.fat_reader import FatReader
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_R2
        volume = Fat32Volume()
        volume.add_file("A.wav", self.tagged_wav("A", "Tape", 44100, 40, 12000))
        volume.add_file("B.wav", self.tagged_wav("B", "Tape", 44100, 600, 9000))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        reader = FatReader(rig.session.disk, rig.session.layout["volume_start"])

        def entry():
            data = reader.read("/" + IDENTITY.data_directory + "/mixes.bin")
            if data is None:
                return b""
            banks = [data[offset:offset + 3072] for offset in (0, 3072)]
            valid = [bank for bank in banks if bank[:4] == b"DSMC"]
            bank = max(valid, key=lambda item: struct.unpack_from("<I", item, 8)[0]) if valid else b""
            return bank[512:592]

        def saved_count(count):
            value = entry()
            return len(value) == 80 and struct.unpack_from("<H", value, 68)[0] == count

        rig.press_many("next", 1)
        rig.press("play")
        rig.press_many("next", 2)
        rig.press("play")
        rig.press("play")
        rig.press("play", hold=1300)
        rig.settle()
        machine = rig.machine
        machine.run(until=lambda: saved_count(0), until_ms=machine.milliseconds() + 6000)
        self.assertEqual(30, entry()[78])
        for track, side in ((0, 0), (1, 1)):
            rig.press("back", hold=1300)
            if track == 0:
                rig.press_many("previous", 1)
            rig.press("play")
            if track == 0:
                rig.press_many("next", 7)
            rig.press("play")
            rig.press_many("next", track)
            rig.press("play", hold=1300)
            rig.press_many("next", 5)
            rig.press("play")
            rig.press("play")
            rig.press_many("next", side)
            rig.press("play")
            rig.settle()
            machine.run(until=lambda: saved_count(track + 1), until_ms=machine.milliseconds() + 6000)
            self.assertTrue(saved_count(track + 1), entry().hex())
        rig.press("back", hold=1300)
        rig.press_many("next", 1)
        rig.press("play")
        rig.press_many("next", 2)
        rig.press("play")
        starts, flips = [], []
        machine = rig.machine
        machine.hook_symbol("hal_audio_play", lambda: starts.append((
            machine.uc.reg_read(UC_ARM_REG_R1), machine.uc.reg_read(UC_ARM_REG_R2))))
        machine.hook_symbol("cassette_flip_screen_enter", lambda: flips.append(machine.milliseconds()))
        rig.press("next")
        rig.press("play")
        machine.run(until=lambda: bool(flips), until_ms=machine.milliseconds() + 10000)
        self.assertEqual([(0, 0)], starts)
        self.assertEqual(1, len(flips))
        rig.press("play")
        self.assertEqual(2, len(starts))
        rig.run_for(1000)
        rig.press("play")
        reader = FatReader(rig.session.disk, rig.session.layout["volume_start"])
        saved = reader.read("/" + IDENTITY.data_directory + "/session.bin")
        records = [saved[offset:offset + 512] for offset in (0, 512)]
        newest = max(records, key=lambda record: struct.unpack_from("<I", record, 8)[0])
        position = struct.unpack_from("<I", newest, 28 + 32)[0]
        self.assertGreater(position, 500)
        self.assertEqual(1, newest[28 + 268 + 9])
        again = rig.session.reboot()
        resumed = Rig.__new__(Rig)
        resumed.session, resumed.machine = again, again.machine
        resumed.audio = AudioPath(again.machine)
        resumed.steps = again.machine.trace("app_step")
        again.run_to_home_screen()
        resumed.settle(1000)
        restored = []
        again.machine.hook_symbol("hal_audio_play", lambda: restored.append(
            again.machine.uc.reg_read(UC_ARM_REG_R1)))
        resumed.press("play")
        resumed.press("play")
        self.assertEqual([position], restored)
        self.assert_healthy(again)

    def test_a_saved_moment_replays_from_its_position_after_restart(self):
        from echo_emulator.audio import AudioPath
        from echo_emulator.fat_reader import FatReader
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        from unicorn.arm_const import UC_ARM_REG_R1
        volume = Fat32Volume()
        volume.add_file("Moment.wav", self.tagged_wav("Moment", "Saved", 44100, 600, 12000))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(1500)
        rig.press("back", hold=1300)
        rig.press_many("next", HOME_LISTENING_ROW)
        rig.press("play")
        rig.press_many("next", 5)
        rig.press("play")
        rig.press("play")
        reader = FatReader(rig.session.disk, rig.session.layout["volume_start"])
        path = "/" + IDENTITY.data_directory + "/moments.bin"
        rig.machine.run(until=lambda: reader.exists(path) and reader.read(path)[:4] == b"DSMO",
                        until_ms=rig.machine.milliseconds() + 6000)
        saved = reader.read(path)
        self.assertEqual(b"DSMO", saved[:4])
        position = struct.unpack_from("<I", saved, 512 + 268)[0]
        self.assertGreater(position, 1000)
        again = rig.session.reboot()
        resumed = Rig.__new__(Rig)
        resumed.session, resumed.machine = again, again.machine
        resumed.audio = AudioPath(again.machine)
        resumed.steps = again.machine.trace("app_step")
        again.run_to_home_screen()
        resumed.settle(1000)
        resumed.press_many("next", HOME_LISTENING_ROW)
        resumed.press("play")
        resumed.press_many("next", 5)
        resumed.press("play")
        starts = []
        again.machine.hook_symbol("hal_audio_play", lambda: starts.append(
            again.machine.uc.reg_read(UC_ARM_REG_R1)))
        resumed.press("next")
        resumed.press("play")
        again.machine.run(until=lambda: bool(starts), until_ms=again.machine.milliseconds() + 6000)
        self.assertEqual([position], starts)
        self.assert_healthy(again)


if __name__ == "__main__":
    sys.exit(main())
