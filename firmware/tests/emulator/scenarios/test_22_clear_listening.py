import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, HOME_LISTENING_ROW, IDENTITY, main


class EchoImageTest(EchoImageHarness):
    def test_clear_listening_exports_plays_then_shows_empty_stats(self):
        from echo_emulator.fat_reader import FatReader
        from echo_emulator.playback import Rig

        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  module_elfs=self.module_elfs)
        rig.open_songs()
        rig.play_song(1)
        rig.skip("next")
        rig.skip("next")
        rig.press("play")
        rig.press("back", hold=1300)
        rig.press_many("next", HOME_LISTENING_ROW)
        rig.press("play")
        rig.press_many("next", 3)
        rig.press("play")
        populated = tuple(rig.machine.panel.frame)
        reader = FatReader(rig.session.disk, rig.session.layout["volume_start"])
        path = "/" + IDENTITY.data_directory + "/listens.bin"

        def root():
            data = reader.read(path)
            banks = [data[offset:offset + 512] for offset in (0, 512)]
            return max((bank for bank in banks if bank[:4] == b"DSLL"),
                       key=lambda bank: struct.unpack_from("<I", bank, 8)[0])

        before = root()
        self.assertGreaterEqual(struct.unpack_from("<I", before, 44)[0], 2)
        rig.press("play", hold=1300)
        rig.press("play")
        rig.press("next")
        rig.press("play")
        machine = rig.machine
        self.assertEqual("condition met", machine.run(
            until=lambda: struct.unpack_from("<I", root(), 32)[0] == 0,
            until_ms=machine.milliseconds() + 10000))
        empty = root()
        self.assertEqual(0, struct.unpack_from("<Q", empty, 36)[0])
        self.assertEqual(0, struct.unpack_from("<I", empty, 44)[0])
        exported = reader.read("/.scrobbler.log")
        self.assertTrue(exported.startswith(b"#AUDIOSCROBBLER/1.1\n#TZ/UTC\n"))
        records = [line for line in exported.splitlines() if not line.startswith(b"#")]
        self.assertGreaterEqual(len(records), 2)
        self.assertTrue(all(len(line.split(b"\t")) == 8 for line in records))
        rig.settle()
        rig.press("play")
        self.assertNotEqual(populated, tuple(machine.panel.frame))
        self.assertEqual(0, struct.unpack_from("<I", root(), 44)[0])
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
