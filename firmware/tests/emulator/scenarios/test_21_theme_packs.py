import json
import struct
import sys
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
    import tempfile
    manifest = json.loads((ROOT / "examples/themes/pocket-scope/theme.json").read_text())
    manifest["assets"] = []
    with tempfile.TemporaryDirectory() as directory:
        (Path(directory) / "theme.json").write_text(json.dumps(manifest))
        return theme_pack.build(Path(directory))


def art_pack(slot, rows, codec="indexed"):
    import tempfile
    from PIL import Image
    manifest = json.loads((ROOT / "examples/themes/pocket-scope/theme.json").read_text())
    manifest["assets"] = [{"slot": slot, "file": "art.png", "orientation": "landscape",
                           "codec": codec}]
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        image = Image.new("RGB", (len(rows[0]), len(rows)))
        image.putdata([pixel for row in rows for pixel in row])
        image.save(folder / "art.png")
        (folder / "theme.json").write_text(json.dumps(manifest))
        return theme_pack.build(folder)


def stripes(width=480, height=222):
    return [[((x * 7) & 255, (y * 5) & 255, ((x + y) * 3) & 255) for x in range(width)]
            for y in range(height)]


def rgb565(pixel):
    red, green, blue = pixel
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


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
        rig.press_many("next", 2)
        rig.press("play")
        rig.press_many("next", 2)
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

    def test_the_first_frame_of_the_splash_shows_the_pack_splash_art(self):
        rows = stripes()
        art = [rgb565(pixel) for row in rows for pixel in row]
        session = self.session(volume=volume(art_pack("splash", rows, "lossless"), active=True))
        self.assertEqual(session.run_to_first_frame(), "condition met")
        frame = session.machine.panel.frame
        share = sum(1 for shown, wanted in zip(frame, art) if shown == wanted) / len(art)
        self.assertGreaterEqual(share, 0.5, share)
        self.assert_healthy(session)

    def home_art_share(self, damaged_slots):
        import make_image
        rows = stripes()
        art = [rgb565(pixel) for row in rows for pixel in row]
        session = self.session(volume=volume(art_pack("home", rows, "lossless"), active=True))
        for slot in damaged_slots:
            session.change_module(slot, make_image.EXTENSION_MODULE_NUMBER, 600,
                                  lambda byte: byte ^ 0xFF)
        self.assertEqual(session.run_to_home_screen(), "condition met")
        session.machine.run(until_ms=session.machine.milliseconds() + 1500)
        frame = session.machine.panel.frame
        self.assert_healthy(session)
        return sum(1 for shown, wanted in zip(frame, art) if shown == wanted) / len(art)

    def test_home_art_comes_from_the_extension_and_falls_back_to_the_palette_without_it(self):
        self.assertGreaterEqual(self.home_art_share(()), 0.3)
        self.assertLessEqual(self.home_art_share(("slot_1", "slot_2")), 0.01)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
