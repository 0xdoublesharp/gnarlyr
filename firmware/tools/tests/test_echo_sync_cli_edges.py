import contextlib
import io
import os
import runpy
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import cover_export
import echo_sync
import library_index
from test_library_index import write_silent_flac


class ToolCliBoundaryTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.music = self.root / "Music"
        self.music.mkdir()
        self.output = io.StringIO()
        self.errors = io.StringIO()
        for redirect in (contextlib.redirect_stdout(self.output), contextlib.redirect_stderr(self.errors)):
            redirect.__enter__()
            self.addCleanup(redirect.__exit__, None, None, None)

    def execute(self, module, args, expected):
        with mock.patch.object(sys, "argv", [str(TOOLS / f"{module}.py"), *map(str, args)]):
            with self.assertRaises(SystemExit) as ended:
                runpy.run_path(str(TOOLS / f"{module}.py"), run_name="__main__")
        self.assertEqual(expected, ended.exception.code)

    def test_real_cli_entrypoints_read_music_and_report_failures_and_missing_playlist_entries(self):
        track = self.music / "song.flac"
        write_silent_flac(track, 1)
        before = track.read_bytes()
        (self.music / "Complete.m3u8").write_text("song.flac\n")
        (self.music / "Mix.m3u8").write_text("song.flac\nmissing.flac\n")
        database = self.root / "output" / "library.db"
        self.execute("library_index", ["--music", self.music, "--output", database], 0)
        self.assertTrue(database.read_bytes().startswith(b"DSDB"))
        self.assertIn("entries not found", self.errors.getvalue())
        (self.music / "broken.flac").write_bytes(b"broken")
        self.execute("library_index", ["--music", self.music, "--output", database], 1)
        self.assertIn("skipped broken.flac", self.errors.getvalue())
        covers = self.root / "output" / "covers.bin"
        self.execute("cover_export", ["--music", self.music, "--output", covers], 0)
        self.execute("cover_export", ["--music", self.music, "--library", database, "--output", covers], 0)
        self.assertTrue(covers.read_bytes().startswith(b"DSCV"))
        (self.music / echo_sync.DATA_FOLDER).mkdir()
        self.execute("echo_sync", [self.music, "--dry-run", "--no-covers", "--yes"], 0)
        self.assertEqual(before, track.read_bytes())
        self.assertEqual([], list((self.music / echo_sync.DATA_FOLDER).iterdir()))

    def test_sync_reports_unreadable_tags_missing_art_and_output_failures(self):
        track = self.music / "song.flac"
        write_silent_flac(track, 1)
        (self.music / "broken.flac").write_bytes(b"bad")
        (self.music / "Complete.m3u").write_text("song.flac\n")
        (self.music / "Mix.m3u").write_text("missing.flac\n")
        model, _ = echo_sync.build_library(self.music)
        self.assertIn("skipped (tags unreadable)", self.output.getvalue())
        self.assertIn("not found", self.output.getvalue())
        with mock.patch.object(library_index, "encode_library", side_effect=ValueError("too many tracks")):
            with self.assertRaisesRegex(echo_sync.SyncError, "too big"):
                echo_sync.build_library(self.music)
        echo_sync.build_covers(self.music, cover_export.album_sources(model), echo_sync.card_volume("fat"), echo_sync.SyncOptions(verbose=True))
        self.assertIn("no cover:", self.output.getvalue())
        with mock.patch.object(echo_sync, "write_card_file", side_effect=OSError("disk failed")):
            with self.assertRaisesRegex(echo_sync.SyncError, "couldn't write"):
                echo_sync.write_or_report(self.root / "library.db", b"data", echo_sync.LIBRARY_FILE, echo_sync.SyncOptions())

    def test_missing_folders_empty_selection_and_next_steps_are_explicit(self):
        self.assertIsNone(echo_sync.host_lister(echo_sync.card_volume("fat"))(self.root / "missing"))
        with mock.patch.object(Path, "is_dir", side_effect=OSError("unavailable")):
            self.assertEqual([], echo_sync.player_cards([self.root]))
        with self.assertRaises(echo_sync.SyncError):
            echo_sync.ask_for_card([], lambda _: "")
        echo_sync.check_cache_targets(self.root / "absent")
        with mock.patch.object(os.path, "ismount", return_value=True):
            echo_sync.check_target(self.music, echo_sync.card_volume("fat"), False)
        echo_sync.ensure_data_folder(self.music / "new-cache", echo_sync.SyncOptions(), True)
        echo_sync.print_next_steps(echo_sync.SyncOptions(), True, echo_sync.CardVolume("FAT32", True, False, 0, ""), 1)
        for fingerprint in (False, True):
            echo_sync.print_next_steps(echo_sync.SyncOptions(library=True, covers=False, fingerprint=fingerprint), False, echo_sync.card_volume("fat"), 1)
        self.assertIn("Covers were left", self.output.getvalue())
        self.assertIn("scan the card once more", self.output.getvalue())
        self.assertIn("compares the music", self.output.getvalue())

    def test_cache_parent_replacement_is_detected_before_opening_the_leaf(self):
        folder = self.music / echo_sync.DATA_FOLDER
        folder.mkdir()
        path = folder / "library.db"
        path.write_bytes(b"untouched")
        original = os.fstat
        def changed(descriptor):
            info = original(descriptor)
            return SimpleNamespace(st_dev=info.st_dev, st_ino=info.st_ino + 1)
        with mock.patch.object(os, "fstat", side_effect=changed):
            with self.assertRaisesRegex(echo_sync.SyncError, "folder changed"):
                with echo_sync.open_card_file(path):
                    self.fail("changed directory admitted")
        self.assertEqual(b"untouched", path.read_bytes())

    def test_cached_library_size_and_every_section_bound_are_checked_before_parsing(self):
        folder = self.music / echo_sync.DATA_FOLDER
        folder.mkdir()
        path = folder / "library.db"
        with path.open("wb") as handle:
            handle.truncate(echo_sync.MAX_LIBRARY_BYTES + 1)
        with self.assertRaisesRegex(ValueError, "too large"):
            echo_sync.cached_album_sources(path)
        raw = library_index.encode_library(library_index.build_library([]))
        for field, value in ((0, 0), (1, 1), (2, 999), (3, 1)):
            data = bytearray(raw)
            struct.pack_into("<I", data, library_index.HEADER_PREFIX.size + field * 4, value)
            path.write_bytes(data)
            with self.subTest(field=field), self.assertRaises(ValueError):
                echo_sync.cached_album_sources(path)
        data = bytearray(raw)
        width = library_index.TRACK_RECORD.size
        data.extend(bytes(65536 * width))
        library_index.SECTION_ENTRY.pack_into(data, library_index.HEADER_PREFIX.size, library_index.HEADER_SIZE, 65536, width, 0)
        path.write_bytes(data)
        with self.assertRaises(ValueError):
            echo_sync.cached_album_sources(path)


if __name__ == "__main__":
    unittest.main()
