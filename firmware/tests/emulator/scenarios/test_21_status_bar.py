import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, FNV_START, FNV_PRIME, IDENTITY, main, struct


class EchoImageTest(EchoImageHarness):
    def test_volume_and_queue_position_follow_live_playback_changes(self):
        from echo_emulator.playback import Rig
        from echo_emulator.real_volume import real_volume
        from unicorn.arm_const import UC_ARM_REG_R1

        volume = real_volume(playable=True, album_count=2)
        volume.add_directory(IDENTITY.data_directory)
        settings = bytearray(self.saved_settings(4, 15, shuffle=0))
        settings[13], settings[26], settings[51] = 13, 1, 5
        checksum = FNV_START
        for value in settings[:54]:
            checksum = (checksum ^ value) * FNV_PRIME & 0xFFFFFFFF
        settings[54:58] = struct.pack("<I", checksum)
        volume.add_file("settings.bin", bytes(settings), directory=IDENTITY.data_directory)
        seen = []

        def prepare(machine):
            def drawn():
                state = machine.reg(UC_ARM_REG_R1)
                volume_text, position = struct.unpack("<II", machine.read_memory(state + 16, 8))
                seen.append((machine.read_cstring(volume_text).decode("ascii") if volume_text else None,
                             machine.read_cstring(position).decode("ascii") if position else None))
            machine.hook_symbol("status_bar_layout", drawn)

        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs, prepare=prepare)
        rig.press("play")
        rig.press_many("previous", 7)
        rig.press("play")
        rig.press("previous")
        rig.press("play")
        self.assertTrue(seen)
        first_volume, first_position = seen[-1]
        self.assertEqual(first_volume, "13")
        self.assertTrue(first_position.startswith("2/"), seen[-1])
        total = first_position.split("/")[1]
        rig.press("next")
        self.assertEqual(seen[-1], ("14", "2/" + total))
        rig.press("back")
        rig.press("previous")
        rig.press("play")
        self.assertEqual(seen[-1], ("14", "3/" + total))
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
