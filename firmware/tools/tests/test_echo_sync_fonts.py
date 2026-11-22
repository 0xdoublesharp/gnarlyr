import contextlib
import io
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1]
FIXTURES = TOOLS.parent / "tests" / "fixtures"
sys.path.insert(0, str(TOOLS))

import echo_sync
import font_recipe
from test_echo_sync import EXFAT, FIXED_UTC
from test_font_recipe import NOTO, covered_text, test_setup
from test_library_index import write_silent_flac


class SyncFontsTest(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        drives = Path(scratch.name) / "drives"
        shutil.copytree(FIXTURES / "combined_music", drives)
        self.card = drives / "card"
        self.internal = drives / "internal"
        (self.card / echo_sync.DATA_FOLDER).mkdir()
        generation = self.internal / echo_sync.DATA_FOLDER / "music_gen.bin"
        generation.parent.mkdir()
        generation.write_bytes(struct.pack("<I", 19))
        self.font = self.internal / echo_sync.DATA_FOLDER / "fonts" / "extra.font"
        for patcher in (mock.patch.object(font_recipe, "default_setup", side_effect=test_setup),
                        mock.patch.object(font_recipe, "cache_directory", return_value=NOTO),
                        mock.patch.object(font_recipe, "open_url", side_effect=AssertionError("no downloads"))):
            patcher.start()
            self.addCleanup(patcher.stop)

    def run_sync(self, *options, internal=True):
        output = io.StringIO()
        mounts = [(0, 0, str(self.internal)), (0, 1, str(self.card))] if internal else []
        with (contextlib.redirect_stdout(output), contextlib.redirect_stderr(output),
              mock.patch.object(echo_sync.sys, "platform", "darwin"),
              mock.patch.object(echo_sync, "mac_player_mounts", return_value=mounts),
              mock.patch.object(echo_sync, "detect_volume",
                                side_effect=lambda path: EXFAT if path == self.card else FIXED_UTC)):
            status = echo_sync.main([str(self.card), *(options or ["--no-covers"])])
        self.assertEqual(0, status, output.getvalue())
        return output.getvalue()

    def add_title(self):
        folder = self.card / ("Artist " + covered_text(6))
        folder.mkdir()
        write_silent_flac(folder / (covered_text(3, 20) + ".flac"), 1)

    def test_a_sync_builds_the_fonts_the_library_needs_on_internal_storage(self):
        self.add_title()
        output = self.run_sync()
        self.assertIn("Fonts for the player...", output)
        self.assertIn("9 characters", output)
        faces, _ = font_recipe.glyph_pack.read_extra_font(self.font.read_bytes())
        self.assertEqual(9, len(faces[0]))
        self.assertFalse((self.card / echo_sync.DATA_FOLDER / "fonts").exists())
        self.assertIn("unchanged", self.run_sync())

    def test_a_latin_library_needs_no_file_and_a_dry_run_writes_none(self):
        self.assertIn("none needed", self.run_sync())
        self.assertFalse(self.font.parent.exists())
        self.add_title()
        self.assertIn("would write 9 characters", self.run_sync("--dry-run"))
        self.assertFalse(self.font.parent.exists())

    def test_fonts_are_skipped_without_internal_storage_or_when_turned_off(self):
        self.assertIn("skipped: connect the player over USB", self.run_sync(internal=False))
        for option in ("--no-fonts", "--covers-only"):
            option = [option] if option == "--covers-only" else ["--no-covers", option]
            with mock.patch.object(font_recipe, "rebuild") as rebuild:
                self.assertNotIn("Fonts for the player", self.run_sync(*option))
            rebuild.assert_not_called()

    def test_a_font_failure_is_reported_without_failing_the_sync(self):
        with mock.patch.object(font_recipe, "rebuild", side_effect=font_recipe.RecipeError("offline")):
            self.assertIn("not updated: offline", self.run_sync())

    def test_skipped_inputs_are_listed(self):
        (self.card / echo_sync.DATA_FOLDER / "lang").mkdir()
        (self.card / echo_sync.DATA_FOLDER / "lang" / "xx.pack").write_bytes(b"broken")
        self.assertIn("  skipped xx.pack", self.run_sync())


if __name__ == "__main__":
    unittest.main()
