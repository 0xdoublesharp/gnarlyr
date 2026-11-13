import contextlib
import io
import os
import sys
import struct
import zlib
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import echo_sync


class CacheConfinementTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.card = self.root / "card"
        self.folder = self.card / echo_sync.DATA_FOLDER
        self.folder.mkdir(parents=True)
        self.outside = self.root / "outside"
        self.outside.mkdir()
        self.original = b"private data outside the selected card"
        self.victim = self.outside / "original.bin"
        self.victim.write_bytes(self.original)
        self.spec = echo_sync.CardFile("library.db", 256, 16)

    def assert_preserved(self, operation):
        with self.assertRaises((echo_sync.SyncError, OSError)):
            operation()
        self.assertEqual(self.original, self.victim.read_bytes())

    def test_a_cache_symlink_never_overwrites_a_file_outside_the_card(self):
        target = self.folder / self.spec.name
        target.symlink_to(self.victim)
        self.assert_preserved(lambda: echo_sync.write_card_file(target, b"new library", self.spec))

    def test_a_data_directory_symlink_never_redirects_publication(self):
        self.folder.rmdir()
        self.folder.symlink_to(self.outside, target_is_directory=True)
        target = self.folder / "original.bin"
        self.assert_preserved(lambda: echo_sync.write_card_file(target, b"new library", self.spec))

    def test_a_cache_hardlink_never_changes_an_unmanaged_file(self):
        target = self.folder / self.spec.name
        os.link(self.victim, target)
        self.assert_preserved(lambda: echo_sync.write_card_file(target, b"new library", self.spec))

    def test_forgetting_a_fingerprint_refuses_a_symlink(self):
        target = self.folder / "scan.bin"
        target.symlink_to(self.victim)
        self.assert_preserved(lambda: echo_sync.forget_fingerprint(target))

    def test_a_dangling_symlink_never_creates_a_file_outside_the_card(self):
        missing = self.outside / "new.bin"
        target = self.folder / self.spec.name
        target.symlink_to(missing)
        with self.assertRaises((echo_sync.SyncError, OSError)):
            echo_sync.write_card_file(target, b"new library", self.spec)
        self.assertFalse(missing.exists())

    def test_sync_refuses_a_redirected_data_folder_before_changing_any_cache(self):
        self.folder.rmdir()
        self.folder.symlink_to(self.outside, target_is_directory=True)
        existing = self.outside / "library.db"
        existing.write_bytes(self.original)
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises((echo_sync.SyncError, OSError)):
                echo_sync.ensure_data_folder(self.folder, echo_sync.SyncOptions(), True)
        self.assertEqual(self.original, existing.read_bytes())

    def test_covers_only_rejects_parent_traversal_from_an_existing_library(self):
        source = echo_sync.cover_export.AlbumSource(1, "Album", "Artist", ["../outside/song.flac"])
        options = echo_sync.SyncOptions(library=False, fingerprint=False)
        with mock.patch.object(echo_sync, "album_sources", return_value=[source]):
            with mock.patch.object(echo_sync.cover_export, "collect_covers") as collect:
                with contextlib.redirect_stdout(io.StringIO()):
                    with self.assertRaises(echo_sync.SyncError):
                        echo_sync.sync(self.card, echo_sync.card_volume("fat32"), options)
                collect.assert_not_called()

    def test_sync_refuses_a_visible_media_symlink_before_parsing_or_writing(self):
        (self.card / "song.flac").symlink_to(self.victim)
        with mock.patch.object(echo_sync.library_index, "scan", return_value=([], [], [])) as scan:
            with contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(echo_sync.SyncError):
                    echo_sync.sync(self.card, echo_sync.card_volume("fat32"), echo_sync.SyncOptions(covers=False))
            scan.assert_not_called()
        self.assertEqual([], list(self.folder.iterdir()))
        self.assertEqual(self.original, self.victim.read_bytes())

    def test_cli_reports_an_unsafe_cache_as_an_error_without_a_traceback_or_writes(self):
        (self.folder / self.spec.name).symlink_to(self.victim)
        with mock.patch.object(echo_sync, "detect_volume", return_value=echo_sync.card_volume("fat32")):
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()) as output:
                self.assertEqual(2, echo_sync.main([str(self.card), "--no-covers"]))
        self.assertIn("echo_sync:", output.getvalue())
        self.assertEqual(self.original, self.victim.read_bytes())

    def test_corrupt_and_unsupported_cached_libraries_are_rebuilt_instead_of_parsed(self):
        empty = echo_sync.library_index.build_library([])
        valid = echo_sync.library_index.encode_library(empty)
        unsupported = bytearray(valid)
        struct.pack_into("<H", unsupported, 4, 2)
        for raw in (b"DSDB", bytes(unsupported)):
            path = self.folder / "library.db"
            path.write_bytes(raw)
            with mock.patch.object(echo_sync, "build_library", return_value=(empty, valid)) as rebuild:
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual([], echo_sync.album_sources(self.card, None, path))
                rebuild.assert_called_once_with(self.card)


class SimulatedPowerLoss(BaseException):
    pass


class FaultFile:
    def __init__(self, file, target=0, crash=False, partial=False, corrupt=""):
        self.file = file
        self.target = target
        self.crash = crash
        self.partial = partial
        self.corrupt = corrupt
        self.events = 0
        self.did_fail = False

    def __getattr__(self, name):
        return getattr(self.file, name)

    def event(self):
        self.events += 1
        if self.events == self.target:
            self.did_fail = True
            if self.crash:
                raise SimulatedPowerLoss()
            raise OSError("simulated disconnect/full disk")

    def write(self, data):
        self.event()
        return self.file.write(data[:3] if self.partial else data)

    def seek(self, *args):
        self.event()
        return self.file.seek(*args)

    def flush(self):
        self.event()
        return self.file.flush()

    def read(self, count):
        self.event()
        position = self.file.tell()
        data = self.file.read(count)
        if data and not self.did_fail and ((self.corrupt == "body" and position > 0) or
                                          (self.corrupt == "header" and position == 0)):
            self.did_fail = True
            return bytes([data[0] ^ 1]) + data[1:]
        return data


class PublicationFaultTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name, "cache.bin")
        self.audio = Path(self.directory.name, "original.flac")
        self.audio.write_bytes(b"audio must remain unchanged")
        self.spec = echo_sync.CardFile("cache.bin", 256, 16)
        self.old = self.encoded(b"old payload" * 5)
        self.new = self.encoded(b"new payload" * 9)

    @staticmethod
    def encoded(body):
        checksum = zlib.crc32(body)
        return struct.pack("<4sIII", b"TEST", len(body), checksum, checksum ^ 0xFFFFFFFF) + body

    @staticmethod
    def decoded(data):
        if len(data) < 16:
            return None
        magic, length, checksum, complement = struct.unpack_from("<4sIII", data)
        body = data[16:16 + length]
        if magic != b"TEST" or checksum ^ complement != 0xFFFFFFFF or len(body) != length:
            return None
        return body if zlib.crc32(body) == checksum else None

    @contextlib.contextmanager
    def wrapping(self, **options):
        original = echo_sync.open_card_file
        wrapped = []

        @contextlib.contextmanager
        def opening(*args, **kwargs):
            with original(*args, **kwargs) as file:
                proxy = FaultFile(file, **options)
                wrapped.append(proxy)
                yield proxy

        with mock.patch.object(echo_sync, "open_card_file", side_effect=opening):
            yield wrapped

    def test_every_write_seek_flush_and_read_interruption_leaves_old_new_or_rejected_cache(self):
        self.path.write_bytes(self.old)
        with self.wrapping() as observed:
            echo_sync.write_card_file(self.path, self.new, self.spec)
        count = observed[0].events
        self.assertGreater(count, 10)
        for crash in (False, True):
            for cut in range(1, count + 1):
                with self.subTest(crash=crash, cut=cut):
                    self.path.write_bytes(self.old)
                    with self.wrapping(target=cut, crash=crash):
                        with self.assertRaises(SimulatedPowerLoss if crash else OSError):
                            echo_sync.write_card_file(self.path, self.new, self.spec)
                    accepted = self.decoded(self.path.read_bytes())
                    self.assertIn(accepted, (None, self.old[16:], self.new[16:]))
                    self.assertEqual(b"audio must remain unchanged", self.audio.read_bytes())
                    echo_sync.write_card_file(self.path, self.new, self.spec)
                    self.assertEqual(self.new[16:], self.decoded(self.path.read_bytes()))

    def test_short_writes_complete_and_readback_corruption_invalidates_the_header(self):
        with self.wrapping(partial=True):
            echo_sync.write_card_file(self.path, self.new, self.spec)
        self.assertEqual(self.new, self.path.read_bytes()[:len(self.new)])
        for phase in ("body", "header"):
            self.path.write_bytes(self.old)
            with self.wrapping(corrupt=phase):
                with self.assertRaisesRegex(OSError, "readback"):
                    echo_sync.write_card_file(self.path, self.new, self.spec)
            self.assertEqual(bytes(16), self.path.read_bytes()[:16])

    def test_fsync_failures_stop_publication_and_a_retry_is_verified(self):
        original = os.fsync
        for cut in (1, 2, 3):
            self.path.write_bytes(self.old)
            calls = 0

            def settle(descriptor):
                nonlocal calls
                calls += 1
                if calls == cut:
                    raise OSError("fsync failed")
                return original(descriptor)

            with mock.patch.object(os, "fsync", side_effect=settle):
                with self.assertRaises(OSError):
                    echo_sync.write_card_file(self.path, self.new, self.spec)
            self.assertEqual(bytes(16), self.path.read_bytes()[:16])
            echo_sync.write_card_file(self.path, self.new, self.spec)
            self.assertEqual(self.new[16:], self.decoded(self.path.read_bytes()))

    def test_cache_open_falls_back_safely_on_hosts_without_directory_relative_open(self):
        with mock.patch.object(os, "supports_dir_fd", set()):
            echo_sync.write_card_file(self.path, self.new, self.spec)
        self.assertEqual(self.new[16:], self.decoded(self.path.read_bytes()))

    def test_zero_length_writes_and_empty_fingerprints_do_not_loop(self):
        class RefusingFile:
            def write(self, _):
                return 0
        with self.assertRaisesRegex(OSError, "stopped"):
            echo_sync.write_all(RefusingFile(), b"payload")
        self.path.write_bytes(b"")
        echo_sync.forget_fingerprint(self.path)
        self.assertEqual(b"", self.path.read_bytes())


if __name__ == "__main__":
    unittest.main()
