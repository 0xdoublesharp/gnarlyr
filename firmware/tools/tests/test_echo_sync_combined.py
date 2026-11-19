import contextlib
import io
import re
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

import cover_export
import echo_sync
from test_cover_export import image_bytes
from test_echo_sync import EXFAT, FIXED_UTC, MEBIBYTE, SCAN_RECORD


class CombinedSyncTest(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        drives = Path(scratch.name) / "drives"
        shutil.copytree(FIXTURES / "combined_music", drives)
        self.card = drives / "card"
        self.internal = drives / "internal"
        self.cache = self.card / echo_sync.DATA_FOLDER
        self.cache.mkdir()
        self.generation = self.internal / echo_sync.DATA_FOLDER / "music_gen.bin"
        self.generation.parent.mkdir()
        self.generation.write_bytes(struct.pack("<I", 19))
        self.library = self.cache / "library.db"
        self.scan = self.cache / "scan.bin"
        self.covers = self.cache / "covers.bin"

    def run_sync(self, *options, card=None, mounts=None):
        output = io.StringIO()
        if mounts is None:
            mounts = [(0, 0, str(self.internal)), (0, 1, str(self.card))]
        with (contextlib.redirect_stdout(output), contextlib.redirect_stderr(output),
              mock.patch.object(echo_sync.sys, "platform", "darwin"),
              mock.patch.object(echo_sync, "mac_player_mounts", return_value=mounts),
              mock.patch.object(echo_sync, "detect_volume",
                                side_effect=lambda path: EXFAT if path == self.card else FIXED_UTC)):
            try:
                result = echo_sync.main([str(card or self.card), *map(str, options)])
            except SystemExit as error:
                result = error.code
        return result, output.getvalue()

    def card_and_internal(self, *options):
        status, output = self.run_sync("--internal", self.internal, *options)
        self.assertEqual(0, status, output)
        return output

    def test_combined_sync_matches_the_shared_firmware_fixture_and_records_both_drives(self):
        self.card_and_internal("--no-covers")
        expected = (FIXTURES / "combined_music.db").read_bytes()
        self.assertEqual(expected.ljust(4 * MEBIBYTE, b"\0"), self.library.read_bytes())
        card = echo_sync.card_fingerprint(self.card, EXFAT)
        internal = echo_sync.card_fingerprint(self.internal, FIXED_UTC)
        self.assertEqual((b"DSS2", card.first, card.second, card.entries, bytes(16),
                          internal.first, internal.second, internal.entries, 1, 19),
                         SCAN_RECORD.unpack_from(self.scan.read_bytes()))
        self.assertEqual([self.generation], list(self.generation.parent.iterdir()))
        self.assertEqual(struct.pack("<I", 19), self.generation.read_bytes())
        self.card_and_internal("--check")
        self.generation.write_bytes(struct.pack("<I", 20))
        status, output = self.run_sync("--internal", self.internal, "--check")
        self.assertEqual(1, status, output)

    def test_covers_only_routes_card_and_internal_art_and_refuses_a_missing_internal_drive(self):
        self.card_and_internal("--no-covers")
        for drive, folder, color in ((self.card, "Card Artist", (255, 0, 0)),
                                     (self.internal, "Inside Artist", (0, 0, 255))):
            (drive / folder / "cover.png").write_bytes(image_bytes(color))
        self.covers.write_bytes(bytes(MEBIBYTE))
        library_before = self.library.read_bytes()
        scan_before = self.scan.read_bytes()
        with mock.patch.object(echo_sync.library_index, "scan", side_effect=AssertionError("rescanned music")):
            self.card_and_internal("--covers-only")
        covers = self.covers.read_bytes()
        self.assertEqual((b"DSCV", 1, 2), struct.unpack_from("<4sHH", covers))
        stamps_start = cover_export.stamped_layout(len(covers))[1]
        stamps = struct.unpack_from("<II", covers, stamps_start)
        expected_stamps = set()
        for drive, relative, volume in ((self.card, "Card Artist", EXFAT),
                                        (self.internal, '"/Inside Artist', FIXED_UTC)):
            image = drive / relative.removeprefix('"/') / "cover.png"
            expected_stamps.add(cover_export.art_stamp(
                f"/{relative}/cover.png".encode(), image.stat().st_size,
                echo_sync.card_stamp(image.stat(), volume), cover_export.FILE_ART))
        self.assertEqual(expected_stamps, set(stamps))
        self.assertEqual(library_before, self.library.read_bytes())
        self.assertEqual(scan_before, self.scan.read_bytes())
        status, output = self.run_sync("--covers-only", mounts=[])
        self.assertEqual(2, status, output)
        self.assertIn("includes music on the player's own storage", output)
        self.assertEqual(covers, self.covers.read_bytes())

    def test_a_folder_cover_on_an_unsupported_internal_filesystem_has_no_stamp(self):
        source = self.internal / "Inside Artist" / "cover.png"
        source.write_bytes(image_bytes((0, 0, 255)))
        drive = echo_sync.MusicDrive(self.internal, echo_sync.card_volume(None))
        describe = echo_sync.art_describer(EXFAT, drive)
        self.assertIsNone(describe(source))

    def test_card_reader_leaves_the_internal_half_unknown(self):
        status, output = self.run_sync("--no-covers", mounts=[])
        self.assertEqual(0, status, output)
        self.assertEqual((0, 0, 0, 2, 0), SCAN_RECORD.unpack_from(self.scan.read_bytes())[5:])
        self.assertNotIn(b'"/Inside Artist/', self.library.read_bytes())

    def test_swapped_arguments_refuse_to_write_to_internal_storage(self):
        self.generation.unlink()
        status, output = self.run_sync("--internal", self.card, "--no-covers", card=self.internal)
        self.assertEqual(2, status, output)
        self.assertIn("internal storage", output)
        self.assertEqual([], list(self.generation.parent.iterdir()))

    def test_lone_detected_internal_mount_is_refused_without_a_generation_file(self):
        self.generation.unlink()
        status, output = self.run_sync("--no-covers", card=self.internal,
                                       mounts=[(0, 0, str(self.internal))])
        self.assertEqual(2, status, output)
        self.assertIn("internal storage", output)
        self.assertEqual([], list(self.generation.parent.iterdir()))

    def test_generation_file_protects_internal_storage_when_detection_fails(self):
        status, output = self.run_sync("--no-covers", card=self.internal, mounts=[])
        self.assertEqual(2, status, output)
        self.assertIn("internal storage", output)
        self.assertEqual([self.generation], list(self.generation.parent.iterdir()))

    def test_picker_prefers_detected_cards_and_excludes_unpaired_internal_mounts(self):
        other_card = self.card.parent / "Other card"
        (other_card / echo_sync.DATA_FOLDER).mkdir(parents=True)
        mounted = [self.internal, other_card, self.card]
        for card_is_attached, expected in ((True, [self.card, other_card]), (False, [other_card])):
            rows = [(1, 0, str(self.internal))]
            if card_is_attached:
                rows.append((0, 1, str(self.card)))
            self.generation.unlink(missing_ok=True)
            with (self.subTest(card_is_attached=card_is_attached),
                  mock.patch.object(echo_sync.sys, "platform", "darwin"),
                  mock.patch.object(echo_sync.sys.stdin, "isatty", return_value=True),
                  mock.patch.object(echo_sync, "mac_player_mounts", return_value=rows),
                  mock.patch.object(echo_sync, "mounted_roots", return_value=mounted if card_is_attached
                                    else mounted[:2]),
                  mock.patch.object(echo_sync, "ask_for_card", return_value=str(expected[0])) as ask):
                echo_sync.drive_roots(echo_sync.parse_arguments([]))
                ask.assert_called_once_with(expected)

    def test_picker_excludes_the_generation_marked_mount_when_detection_fails(self):
        with (mock.patch.object(echo_sync.sys, "platform", "darwin"),
              mock.patch.object(echo_sync.sys.stdin, "isatty", return_value=True),
              mock.patch.object(echo_sync, "mac_player_mounts", return_value=[]),
              mock.patch.object(echo_sync, "mounted_roots", return_value=[self.internal, self.card]),
              mock.patch.object(echo_sync, "ask_for_card", return_value=str(self.card)) as ask):
            echo_sync.drive_roots(echo_sync.parse_arguments([]))
        ask.assert_called_once_with([self.card])

    def test_detected_card_includes_internal_storage_without_an_explicit_argument(self):
        status, output = self.run_sync("--no-covers")
        self.assertEqual(0, status, output)
        self.assertIn(b'"/Inside Artist/', self.library.read_bytes()[:4096])

    def test_internal_music_changes_during_sync_leave_a_valid_unknown_record(self):
        self.card_and_internal("--no-covers")
        write = echo_sync.write_card_file
        before = echo_sync.card_fingerprint(self.internal, FIXED_UTC)

        def write_with_change(path, data, card_file):
            result = write(path, data, card_file)
            if path == self.library:
                (self.internal / "Changed.m3u").write_text("At Root.flac\n")
            return result

        with mock.patch.object(echo_sync, "write_card_file", side_effect=write_with_change):
            output = self.card_and_internal("--no-covers")
        self.assertIn("changed while this ran", output)
        self.assertNotEqual(before, echo_sync.card_fingerprint(self.internal, FIXED_UTC))
        self.assertEqual(struct.pack("<I", 19), self.generation.read_bytes())
        record = SCAN_RECORD.unpack_from(self.scan.read_bytes())
        self.assertEqual((b"DSS2", 0, 0, 0), record[:4])
        self.assertEqual((0, 0, 0, 2, 0), record[5:])

    def test_covers_only_indexes_both_drives_when_no_library_is_present(self):
        self.generation.unlink()
        (self.internal / "Inside Artist" / "cover.png").write_bytes(image_bytes((0, 0, 255)))
        self.covers.write_bytes(bytes(MEBIBYTE))
        self.card_and_internal("--covers-only")
        self.assertEqual((b"DSCV", 1, 1), struct.unpack_from("<4sHH", self.covers.read_bytes()))
        self.assertFalse(self.library.exists())
        self.assertFalse(self.scan.exists())
        self.card_and_internal("--no-covers")
        self.assertEqual((1, 0), SCAN_RECORD.unpack_from(self.scan.read_bytes())[-2:])

    def test_a_cached_internal_path_cannot_escape_to_the_card(self):
        self.card_and_internal("--no-covers")
        source = cover_export.AlbumSource(1, "Album", "Artist", ['"/../card/song.flac'])
        with mock.patch.object(echo_sync, "cached_album_sources", return_value=[source]):
            status, output = self.run_sync("--internal", self.internal, "--covers-only")
        self.assertEqual(2, status, output)
        self.assertIn("outside its music drive", output)
        self.assertFalse(self.covers.exists())

    def test_ambiguous_mount_ownership_refuses_to_select_an_internal_drive(self):
        other = self.internal.parent / "other"
        rows = [(0, 1, str(self.card)), (1, 1, str(self.card)),
                (0, 0, str(self.internal)), (1, 0, str(other))]
        status, output = self.run_sync("--no-covers", mounts=rows)
        self.assertEqual(2, status, output)
        self.assertIn("same player", output)
        self.assertFalse(self.library.exists())

    def test_explicit_internal_requires_the_verified_mount_of_the_same_player(self):
        other = self.internal.parent / "other"
        unrelated = self.internal.parent / "Music"
        for folder in (other, unrelated):
            folder.mkdir()
        rows = [(0, 1, str(self.card)), (0, 0, str(self.internal)), (1, 0, str(other))]
        for internal, mounts in ((other, rows), (unrelated, rows), (self.internal, [])):
            with self.subTest(internal=internal, mounts=mounts):
                status, output = self.run_sync("--internal", internal, "--no-covers", mounts=mounts)
                self.assertEqual(2, status, output)
                self.assertIn("same player", output)
                self.assertFalse(self.library.exists())

    def test_unverified_card_reader_record_matches_firmware_startup_fields(self):
        status, output = self.run_sync("--no-covers", "--no-fingerprint", mounts=[])
        self.assertEqual(0, status, output)
        self.assertTrue(self.scan.is_file(), "startup needs a valid record after a card-reader sync")
        header = (TOOLS.parent / "src/app/music_check.h").read_text()

        def constant(name):
            return int(re.search(rf"\bMUSIC_CHECK_{name} = (0x[\da-f]+|\d+)", header)[1], 0)

        data = self.scan.read_bytes()
        record_bytes = constant("PRINT_BYTES") + constant("VERSION_BYTES") + 20
        self.assertGreaterEqual(len(data), record_bytes)
        self.assertEqual(constant("RECORD_MAGIC"), struct.unpack_from("<I", data)[0])
        state, generation = struct.unpack_from("<II", data, record_bytes - 8)
        self.assertEqual(constant("STATE_UNKNOWN"), state)
        self.assertEqual(0, generation)
        self.assertEqual((0, 0, 0), struct.unpack_from("<III", data, 4))

    def test_auto_detected_internal_cannot_be_nested_inside_the_card(self):
        nested = self.card / "Nested mount"
        nested.mkdir()
        mounts = [(0, 0, str(nested)), (0, 1, str(self.card))]
        status, output = self.run_sync("--no-covers", mounts=mounts)
        self.assertEqual(2, status, output)
        self.assertIn("separate mount points", output)
        self.assertFalse(self.library.exists())

    def test_explicit_internal_must_be_a_separate_directory(self):
        for internal in (self.card, self.card / "Card Artist", self.internal / "missing"):
            with self.subTest(internal=internal):
                status, output = self.run_sync("--internal", internal, "--no-covers")
                self.assertEqual(2, status, output)
                self.assertFalse(self.library.exists())


if __name__ == "__main__":
    unittest.main()
