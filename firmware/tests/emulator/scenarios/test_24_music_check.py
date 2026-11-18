import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    CONFIG,
    EchoImageHarness,
    main,
)

ARTISTS = 600
FOLDERS = ARTISTS * 2 + 1
READS_PER_FOLDER = 3
OPENS_PER_READ = 20
FRAME = bytes.fromhex("fffb9064") + bytes(413)


def many_folders():
    from echo_emulator.fat_volume import Fat32Volume
    volume = Fat32Volume(megabytes=4096, sectors_per_cluster=64, fsinfo=True)
    volume.add_directory("Library")
    for number in range(ARTISTS):
        artist = "Library/Artist with a long folder name %03d" % number
        volume.add_directory(artist.rpartition("/")[2], "Library")
        volume.add_directory("Album with a long folder name", artist)
    volume.add_file("01 First.mp3", FRAME * 40, None, "Library/Artist with a long folder name 000")
    volume.add_file("02 Last.mp3", FRAME * 40, None, "Library/Artist with a long folder name %03d" % (ARTISTS - 1))
    return volume


class EchoImageTest(EchoImageHarness):
    def test_the_music_check_reads_each_folder_about_once_on_a_card_of_many_folders(self):
        from echo_emulator import bench
        artifacts = bench.Artifacts(Path(CONFIG["elf"]).parents[2])
        session, _ = bench.first_scan(artifacts, many_folders(), lambda text: None)
        check = bench.usb_music_check(session, artifacts, lambda text: None)
        walk = check["walk"]
        print("\n[music check] %d folders: %s" % (FOLDERS, walk))
        self.assertIsNotNone(walk)
        self.assertLess(walk["read_commands"], FOLDERS * READS_PER_FOLDER)

    def test_the_scan_opens_each_folder_from_its_listing_on_a_card_of_many_folders(self):
        from echo_emulator import bench
        artifacts = bench.Artifacts(Path(CONFIG["elf"]).parents[2])
        session = artifacts.session(many_folders())
        run = bench.ScanRun(session, artifacts, lambda text: None, covers=False)
        opens = bench.Calls(session, "open_dir", run.profiler.overlay.symbols["open_dir"])
        run.machine.answer_waits_for_key(4)
        run.run()
        scan = [call for call in opens.since(run.started["us"]) if call["end_us"] <= run.ended["us"]]
        reads = sum(call["read_commands"] for call in scan)
        print("\n[scan] %d folder opens, %d read commands" % (len(scan), reads))
        self.assertGreaterEqual(len(scan), FOLDERS)
        self.assertLess(reads, FOLDERS // OPENS_PER_READ)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
