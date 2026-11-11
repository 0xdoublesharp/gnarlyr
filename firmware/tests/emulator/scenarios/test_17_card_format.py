import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    IDENTITY,
    english,
    struct,
)


class EchoImageTest(EchoImageHarness):
    def test_card_format_creates_an_empty_card_then_copied_music_plays(self):
        from echo_emulator.emmc import SdCard, SparseDisk
        from echo_emulator.fat_volume import Fat32Volume, short_name
        from echo_emulator.fat_reader import FatReader
        from echo_emulator.playback import Rig
        card = SparseDisk(2 * 2 * 1024 * 1024)
        inserted = []

        def prepare(machine):
            inserted.append(SdCard(card, machine))
            machine.sd_controller.card = inserted[0]
            machine.card_present = True

        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=Fat32Volume(), module_elfs=self.module_elfs, prepare=prepare)
        internal_before = rig.session.disk.snapshot()
        rig.press("next")
        rig.press("play")
        for _ in range(3):
            rig.press("previous")
        rig.press("play")
        self.assertEqual([], inserted[0].writes)
        rig.press("play")
        self.assertEqual([], inserted[0].writes)
        rig.press("next")
        rig.press("play")
        rig.machine.run(until=lambda: card.read(0, 1)[510:512] == b"\x55\xaa")
        start = struct.unpack_from("<I", card.read(0, 1), 454)[0]
        reader = FatReader(card, start)

        def database_ready():
            found = reader.find(f"/{IDENTITY.data_directory}/library.db")
            if found is None:
                return False
            lba = reader.data_start + (found[1] - 2) * reader.sectors_per_cluster
            return card.read(lba, 1)[:4] == b"DSDB"

        rig.machine.run(until=database_ready)
        rig.settle()
        self.assertTrue(inserted[0].writes)
        self.assertFalse(any(name.lower().endswith(".wav") for name in reader.list("/")))
        pcm = struct.pack("<8h", 0, 2828, 4000, 2828, 0, -2828, -4000, -2828) * (44100 * 20 // 8)
        wav = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                          16, 1, 1, 44100, 88200, 2, 16, b"data", len(pcm)) + pcm
        cluster_bytes = reader.sectors_per_cluster * 512
        count = (len(wav) + cluster_bytes - 1) // cluster_bytes
        first = 3
        while any(reader.next_cluster(cluster) != 0 for cluster in range(first, first + count)):
            first += count
        boot = card.read(start, 1)
        fats = boot[16]
        fat_sectors = struct.unpack_from("<I", boot, 36)[0]
        for cluster in range(first, first + count):
            for copy in range(fats):
                lba = reader.fat_start + copy * fat_sectors + cluster // 128
                sector = bytearray(card.read(lba, 1))
                value = cluster + 1 if cluster < first + count - 1 else 0x0FFFFFFF
                struct.pack_into("<I", sector, cluster % 128 * 4, value)
                card.write(lba, sector)
        card.write(reader.data_start + (first - 2) * reader.sectors_per_cluster,
                   wav.ljust(count * cluster_bytes, b"\0"))
        root_lba = reader.data_start + (reader.root - 2) * reader.sectors_per_cluster
        root = bytearray(card.read(root_lba, reader.sectors_per_cluster))
        offset = next(index for index in range(0, len(root), 32) if root[index] == 0)
        root[offset:offset + 11] = short_name("COPIED.WAV")
        root[offset + 11] = 0x20
        struct.pack_into("<H", root, offset + 20, first >> 16)
        struct.pack_into("<HI", root, offset + 26, first & 0xFFFF, len(wav))
        card.write(root_lba, root)
        self.assertEqual(wav, reader.read("/COPIED.WAV"))
        rig.press("next")
        rig.press("play")
        rig.press_many("next", 20)
        rig.press("play")
        rig.settle(1000)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        self.assertEqual(44100, rig.audio.rate)
        for lba, data in internal_before.items():
            if lba < rig.session.layout["volume_start"]:
                self.assertEqual(data, rig.session.disk.read(lba, 1))
        self.assert_healthy(rig.session)

    def test_card_removal_during_format_reports_failure_and_reinsertion_can_retry(self):
        from echo_emulator.emmc import SdCard, SparseDisk
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        card = SparseDisk(2 * 2 * 1024 * 1024)

        class RemovedCard(SdCard):
            removed = False

            def write_blocks(self, argument, data):
                super().write_blocks(argument, data)
                if len(self.writes) == 3 and not self.removed:
                    self.removed = True
                    self.machine.card_present = False

        inserted = []
        def prepare(machine):
            inserted.append(RemovedCard(card, machine))
            machine.sd_controller.card = inserted[0]
            machine.card_present = True

        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=Fat32Volume(), module_elfs=self.module_elfs, prepare=prepare)
        toasts = self.collect_toasts(rig.machine)
        def confirm():
            rig.press("next")
            rig.press("play")
            for _ in range(3):
                rig.press("previous")
            rig.press("play")
            rig.press("play")
            rig.press("next")
            rig.press("play")

        confirm()
        rig.machine.run(until=lambda: english("STR_CARD_FORMAT_FAILED") in toasts)
        self.assertEqual(3, len(inserted[0].writes))
        self.assertEqual(bytes(512), card.read(0, 1))
        self.assertEqual(0, rig.machine.watchdog_resets)
        rig.settle()
        rig.machine.card_present = True
        confirm()
        rig.machine.run(until=lambda: english("STR_CARD_FORMAT_DONE") in toasts)
        self.assertEqual(b"\x55\xaa", card.read(0, 1)[510:512])
        self.assertEqual(0, rig.machine.watchdog_resets)
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
