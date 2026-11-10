import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import check_identity_literals
from identity import IDENTITY

WORDS = check_identity_literals.forbidden_words()


class ForbiddenWordsTest(unittest.TestCase):
    def test_every_name_of_the_identity_and_every_former_id_is_looked_for(self):
        for name in (IDENTITY.name, IDENTITY.short_name, IDENTITY.manufacturer, IDENTITY.id,
                     IDENTITY.playlist_start_directive, *IDENTITY.former_ids):
            self.assertIn(name.casefold(), WORDS)
        self.assertIn(IDENTITY.data_directory[1:], WORDS)
        self.assertIn(IDENTITY.update_file.split(".")[0].casefold(), WORDS)

    def test_there_is_a_former_id(self):
        self.assertTrue(IDENTITY.former_ids)


class FindViolationsTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def found(self):
        return check_identity_literals.find_violations(self.root, WORDS)

    def test_a_name_in_code_is_reported_with_its_file_and_line(self):
        self.write("src/app/home.c", f'int x;\nconst char *title = "{IDENTITY.short_name}";\n')
        self.assertEqual([f"src/app/home.c:2: {IDENTITY.short_name.casefold()}"], self.found())

    def test_a_name_is_found_in_any_case_and_inside_a_longer_word(self):
        self.write("tools/sync.py", f'FOLDER = "/{IDENTITY.id.lower()}_old"\n')
        self.write("CMakeLists.txt", f"set(IMAGE {IDENTITY.short_name.upper()}.IMG)\n")
        self.assertEqual(2, len(self.found()))

    def test_a_former_id_is_reported(self):
        self.write("tests/device/test_updater.c", f'memcpy(image, "{IDENTITY.former_ids[0]}", 8);\n')
        self.assertEqual([f"tests/device/test_updater.c:1: {IDENTITY.former_ids[0].casefold()}"], self.found())

    def test_the_makers_name_is_reported_too(self):
        self.write("platform/device/logic/usb_device.c", f'u"{IDENTITY.manufacturer}"\n')
        self.assertEqual(1, len(self.found()))

    def test_the_identity_file_documents_and_generated_fixtures_are_not_read(self):
        text = f"{IDENTITY.name} {IDENTITY.id}\n"
        for name in ("identity.json", "README.md", "docs/user-guide.md", "LICENSE",
                     "tests/fixtures/cue_music/Favorite Tracks.m3u", "build/dev/CMakeCache.txt",
                     "third_party/library/notes.txt", ".venv/lib/site.py"):
            self.write(name, text)
        self.assertEqual([], self.found())

    def test_a_folder_named_fixtures_elsewhere_is_read(self):
        self.write("tools/fixtures/notes.txt", IDENTITY.id)
        self.assertEqual(1, len(self.found()))

    def test_a_file_that_is_not_text_is_passed_over(self):
        (self.root / "font.bin").write_bytes(b"\xff\xfe" + IDENTITY.id.encode("ascii"))
        self.assertEqual([], self.found())


if __name__ == "__main__":
    unittest.main()
