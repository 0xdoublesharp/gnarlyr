import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from echo_emulator.fat_reader import FatReader
from echo_emulator.fat_volume import Fat32Volume

SECTOR = 512


class Disk:
    def __init__(self):
        self.sectors = {}

    def write(self, lba, data):
        for index in range(len(data) // SECTOR):
            self.sectors[lba + index] = bytes(data[index * SECTOR:(index + 1) * SECTOR])

    def read(self, lba, count):
        return b"".join(self.sectors.get(lba + index, bytes(SECTOR)) for index in range(count))


class AliasTest(unittest.TestCase):
    def test_a_dozen_names_sharing_a_stem_keep_every_root_entry_readable(self):
        volume = Fat32Volume(64, 1)
        names = ["Velvet Band %d" % number for number in range(1, 13)]
        for name in names:
            volume.add_directory(name)
        volume.add_file("._.DS_Store", b"x" * 10)
        disk = Disk()
        volume.write_to(disk, 0)
        self.assertEqual(sorted(names + ["._.DS_Store"]), FatReader(disk, 0).list("/"))

    def test_aliases_stay_eight_characters_and_unique(self):
        volume = Fat32Volume(64, 1)
        aliases = [volume.alias(volume.root, "Velvet %d" % number) for number in range(1, 120)]
        aliases += [volume.alias(volume.root, "Velve %d" % number) for number in range(1, 20)]
        self.assertEqual({11}, {len(alias) for alias in aliases})
        self.assertEqual(len(aliases), len(set(aliases)))


if __name__ == "__main__":
    unittest.main()
