import json
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, IDENTITY, fnv1a
from echo_emulator.fat_volume import Fat32Volume
from echo_emulator.playback import Rig
from unicorn.arm_const import UC_ARM_REG_R0
import theme_pack

ROOT = Path(__file__).resolve().parents[4]
THEMES = IDENTITY.data_directory + "/themes"
SELECTION = "/" + IDENTITY.data_directory + "/theme.bin"


def pack():
    manifest = json.loads((ROOT / "examples/themes/pocket-scope/theme.json").read_text())
    data = bytearray(512)
    data[:8] = struct.pack("<4sHH", b"DSTP", 1, 512)
    struct.pack_into("<I", data, 8, 512)
    data[16:20] = bytes((0, 0, 20, 0))
    import uuid
    data[20:36] = uuid.UUID(manifest["id"]).bytes
    for field, start, size in (("name", 36, 32), ("author", 68, 32), ("version", 100, 32),
                               ("licence", 132, 64)):
        data[start:start + size] = theme_pack.padded(manifest[field], size)
    struct.pack_into("<20H", data, 452, *(theme_pack.rgb565(manifest["palette"][role]) for role in theme_pack.ROLES))
    struct.pack_into("<I", data, 12, zlib.crc32(data[:12] + data[16:]))
    theme_pack.check(data)
    return bytes(data)


def selection(identity):
    data = bytearray(1024)
    data[28:45] = identity + b"\0"
    struct.pack_into("<4sHHIIII", data, 0, b"DSTS", 1, 0, 1, 17, fnv1a(data[28:45]), 0)
    struct.pack_into("<I", data, 24, fnv1a(data[:24]))
    return bytes(data)


def volume(data, active=False):
    result = Fat32Volume()
    result.add_directory(IDENTITY.data_directory)
    result.add_directory("themes", IDENTITY.data_directory)
    result.add_file("scope.gtheme", data, directory=THEMES)
    if active:
        result.add_file("theme.bin", selection(data[20:36]), directory=IDENTITY.data_directory)
    return result


class EchoImageTest(EchoImageHarness):
    def palette_watch(self, machine):
        address = []
        machine.hook_symbol("default_theme_set_appearance", lambda: address.append(machine.reg(UC_ARM_REG_R0)))
        return address

    def test_a_theme_pack_selected_in_settings_survives_restart(self):
        data = pack()
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume(data), module_elfs=self.module_elfs)
        rig.press("previous")
        rig.press("play")
        rig.press_many("next", 24)
        rig.press("play")
        rig.press_many("next", 4)
        rig.press("play")
        saved = rig.session.fat().read(SELECTION)
        self.assertIsNotNone(saved)
        self.assertEqual(saved[28:44], data[20:36])
        reboot = rig.session.reboot()
        address = self.palette_watch(reboot.machine)
        self.assertEqual(reboot.run_to_home_screen(), "condition met")
        self.assertTrue(address)
        self.assertEqual(reboot.machine.read_memory(address[-1], 40), data[452:492])
        self.assert_only_the_user_volume_changed(rig.session)
        self.assert_healthy(rig.session)
        self.assert_healthy(reboot)

    def test_a_damaged_active_theme_pack_at_boot_falls_back_to_the_default(self):
        data = bytearray(pack())
        data[452] ^= 1
        session = self.session(volume=volume(bytes(data), active=True))
        address = self.palette_watch(session.machine)
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(address)
        background, = struct.unpack("<H", session.machine.read_memory(address[-1], 2))
        self.assertEqual(background, ((18 >> 3) << 11) | ((16 >> 2) << 5) | (34 >> 3))
        self.assertEqual(session.machine.watchdog_resets, 0)
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
