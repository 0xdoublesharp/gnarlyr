import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import save_files


class SaveFilesTest(unittest.TestCase):
    def test_missing_files_are_created_zero_filled(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            save_files.prepare(folder)
            for name in save_files.SAVE_FILES:
                data = (folder / name).read_bytes()
                self.assertEqual(save_files.SAVE_FILE_SIZE, len(data))
                self.assertEqual(bytes(save_files.SAVE_FILE_SIZE), data)

    def test_short_files_keep_their_bytes_and_are_padded(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "settings.bin").write_bytes(b"DSST" + bytes(range(20)))
            save_files.prepare(folder)
            data = (folder / "settings.bin").read_bytes()
            self.assertEqual(save_files.SAVE_FILE_SIZE, len(data))
            self.assertEqual(b"DSST" + bytes(range(20)), data[:24])
            self.assertEqual(bytes(save_files.SAVE_FILE_SIZE - 24), data[24:])

    def test_files_already_big_enough_are_left_alone(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            original = bytes([7]) * (save_files.SAVE_FILE_SIZE + 10)
            (folder / "favorites.bin").write_bytes(original)
            save_files.prepare(folder)
            self.assertEqual(original, (folder / "favorites.bin").read_bytes())

    def test_the_folder_is_created_when_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / ".doublesharp"
            save_files.prepare(folder)
            self.assertTrue((folder / "resume.bin").is_file())


class LibraryRoomTest(unittest.TestCase):
    def test_the_library_gets_its_reserved_room_and_keeps_its_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "library.db").write_bytes(b"DSDB" + bytes(100))
            save_files.prepare(folder)
            data = (folder / "library.db").read_bytes()
            self.assertEqual(save_files.LIBRARY_FILE_SIZE, len(data))
            self.assertEqual(b"DSDB", data[:4])

    def test_a_missing_library_is_reserved_as_zeros(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            save_files.prepare(folder)
            data = (folder / "library.db").read_bytes()
            self.assertEqual(save_files.LIBRARY_FILE_SIZE, len(data))
            self.assertEqual(0, max(data))


if __name__ == "__main__":
    unittest.main()
