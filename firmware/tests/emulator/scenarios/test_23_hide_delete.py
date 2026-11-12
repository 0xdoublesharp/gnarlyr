import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, IDENTITY, main

HOLD_MS = 1300
HIDE_ROW = 6
DELETE_ROW = 7
RECORDS = 8
FIRST_RECORD = 512
RECORD_PATH = FIRST_RECORD + 10


def short_wav():
    pcm = struct.pack("<8h", 0, 2828, 4000, 2828, 0, -2828, -4000, -2828) * 250
    return struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                       16, 1, 1, 8000, 16000, 2, 16, b"data", len(pcm)) + pcm


class EchoImageTest(EchoImageHarness):
    def test_a_song_is_hidden_and_another_deleted_from_its_actions(self):
        from echo_emulator.fat_reader import FatReader
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig

        volume = Fat32Volume()
        for name in ("A.wav", "B.wav", "C.wav"):
            volume.add_file(name, short_wav())
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)

        def card():
            return FatReader(rig.session.disk, rig.session.layout["volume_start"])

        def song_action(row):
            rig.press("play", hold=HOLD_MS)
            rig.press_many("next", row)
            rig.press("play")

        rig.open_songs()
        song_action(HIDE_ROW)
        hidden = card().read("/" + IDENTITY.data_directory + "/hidden.bin")
        self.assertEqual(b"DSHD", hidden[:4])
        self.assertEqual(1, struct.unpack_from("<H", hidden, RECORDS)[0])
        self.assertEqual(b"A.wav", hidden[RECORD_PATH:RECORD_PATH + 5])
        song_action(DELETE_ROW)
        rig.press("play")
        self.assertTrue(card().exists("/B.wav"))
        song_action(DELETE_ROW)
        rig.press("next")
        rig.press("play")
        self.assertFalse(card().exists("/B.wav"))
        self.assertTrue(card().exists("/A.wav"))
        self.assertTrue(card().exists("/C.wav"))
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
