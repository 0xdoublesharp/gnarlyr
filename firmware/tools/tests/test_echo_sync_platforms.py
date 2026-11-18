import contextlib
import ctypes
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import echo_sync


class PlatformBoundaryTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.output = io.StringIO()
        self.redirect = contextlib.redirect_stdout(self.output)
        self.redirect.__enter__()
        self.addCleanup(self.redirect.__exit__, None, None, None)

    def test_windows_file_system_and_drive_probes_handle_success_and_failure_without_hardware(self):
        found = True

        def volume(*args):
            args[6].value = "FAT32"
            return int(found)

        kernel = SimpleNamespace(GetVolumeInformationW=volume, SetErrorMode=mock.Mock(),
                                 GetLogicalDrives=lambda: 0b10001)
        with mock.patch.object(ctypes, "windll", SimpleNamespace(kernel32=kernel), create=True):
            self.assertEqual(("FAT32", ""), echo_sync.windows_file_system(self.root))
            found = False
            self.assertEqual((None, ""), echo_sync.windows_file_system(self.root))
            self.assertEqual([Path("A:\\"), Path("E:\\")], echo_sync.windows_drive_roots())
            kernel.SetErrorMode.assert_called_once_with(echo_sync.SEM_FAILCRITICALERRORS)

    def test_mount_parsers_decode_spaces_ignore_bad_rows_and_survive_command_failures(self):
        with mock.patch.object(Path, "read_text", return_value="/dev/card /media/MY\\040CARD vfat rw,time_offset=-480 0 0\nbad\n"):
            self.assertEqual([("/media/MY CARD", "vfat", "rw,time_offset=-480")], echo_sync.linux_mounts())
        with mock.patch.object(Path, "read_text", side_effect=OSError("unavailable")):
            self.assertEqual([], echo_sync.linux_mounts())
        with mock.patch.object(echo_sync.subprocess, "run", return_value=SimpleNamespace(stdout="disk on /Volumes/CARD (msdos, local, nodev)\ndisk on /Volumes/B (exfat)\nbad\n")):
            self.assertEqual([("/Volumes/CARD", "msdos", "local, nodev"), ("/Volumes/B", "exfat", "")], echo_sync.mac_mounts())
        with mock.patch.object(echo_sync.subprocess, "run", side_effect=OSError("unavailable")):
            self.assertEqual([], echo_sync.mac_mounts())
        for platform, function in (("linux", "linux_mounts"), ("darwin", "mac_mounts")):
            with mock.patch.object(echo_sync.sys, "platform", platform):
                with mock.patch.object(echo_sync, function, return_value=[("/", "unknown", ""), (str(self.root), "vfat", "tz=UTC")]):
                    self.assertEqual(("vfat", "tz=UTC"), echo_sync.file_system_name(self.root))
                with mock.patch.object(echo_sync, function, return_value=[]):
                    self.assertEqual((None, ""), echo_sync.file_system_name(self.root))
        with mock.patch.object(echo_sync.sys, "platform", "win32"), mock.patch.object(echo_sync, "windows_file_system", return_value=("FAT32", "")):
            self.assertEqual(("FAT32", ""), echo_sync.file_system_name(self.root))

    def test_volume_clocks_names_and_mount_enumeration_are_explicit_per_host(self):
        for value, seconds in (("tz=UTC", 0), ("rw,time_offset=-480", -28800), ("unknown", 0)):
            self.assertEqual(seconds, echo_sync.mount_option_offset(value))
        self.assertEqual(3600, echo_sync.card_volume("vfat", "time_offset=60", "linux").fixed_offset)
        with mock.patch.object(echo_sync, "current_offset", return_value=120):
            self.assertEqual(120, echo_sync.card_volume("fat32", "", "win32").fixed_offset)
            self.assertIn("Mac", echo_sync.card_volume("fat", "", "darwin").caveat)
        self.assertEqual(b"caf\xc3\xa9", echo_sync.card_name("cafe\u0301", "darwin"))
        self.assertEqual(b"abc", echo_sync.card_name("abc", "linux"))
        self.assertEqual(b"abc", echo_sync.card_name("abc", "win32"))
        self.assertEqual("x\\ y", echo_sync.typed_path("x\\ y", "win32"))
        self.assertEqual(Path("E:" + os.sep).resolve(), echo_sync.card_root("E:"))
        with mock.patch.object(echo_sync, "windows_drive_roots", return_value=[self.root]):
            self.assertEqual([self.root], echo_sync.mounted_roots("win32"))
        with mock.patch.object(Path, "glob", return_value=iter([self.root])):
            self.assertEqual([self.root], echo_sync.mounted_roots("darwin"))
        with mock.patch.object(Path, "glob", side_effect=lambda _: iter([self.root])), mock.patch.dict(os.environ, {"USER": "tester"}):
            self.assertEqual([self.root, self.root], echo_sync.mounted_roots("linux"))

    def test_missing_cards_dependencies_and_explicit_folder_confirmation_fail_before_writes(self):
        with self.assertRaises(echo_sync.SyncError):
            echo_sync.check_target(self.root / "missing", echo_sync.card_volume("fat"), False)
        with mock.patch.object(os.path, "ismount", return_value=True):
            with self.assertRaises(echo_sync.SyncError):
                echo_sync.check_target(self.root, echo_sync.card_volume("NTFS"), False)
        with mock.patch.object(echo_sync.importlib.util, "find_spec", return_value=None):
            with self.assertRaisesRegex(echo_sync.SyncError, "mutagen.*Pillow"):
                echo_sync.require_dependencies()
        folder = self.root / "cache"
        echo_sync.ensure_data_folder(folder, echo_sync.SyncOptions(dry_run=True), False)
        self.assertFalse(folder.exists())
        with mock.patch.object(sys.stdin, "isatty", return_value=False):
            with self.assertRaises(echo_sync.SyncError):
                echo_sync.ensure_data_folder(folder, echo_sync.SyncOptions(), False)
        with mock.patch.object(sys.stdin, "isatty", return_value=True):
            with mock.patch("builtins.input", return_value="no"):
                with self.assertRaises(echo_sync.SyncError):
                    echo_sync.ensure_data_folder(folder, echo_sync.SyncOptions(), False)
            with mock.patch("builtins.input", return_value="yes"):
                echo_sync.ensure_data_folder(folder, echo_sync.SyncOptions(), False)
        self.assertTrue(folder.is_dir())

    def test_interactive_selection_interruptions_and_close_prompts_have_finite_outcomes(self):
        self.assertEqual(str(self.root), echo_sync.ask_for_card([self.root, self.root / "other"], lambda _: str(self.root)))
        with self.assertRaises(echo_sync.SyncError):
            echo_sync.ask_for_card([], lambda _: "")
        args = SimpleNamespace(card=None)
        with mock.patch.object(sys.stdin, "isatty", return_value=False):
            with self.assertRaises(echo_sync.SyncError):
                echo_sync.card_argument(args)
        with mock.patch.object(sys.stdin, "isatty", return_value=True), mock.patch.object(echo_sync, "mounted_roots", return_value=[]), mock.patch.object(echo_sync, "ask_for_card", return_value=str(self.root)):
            self.assertEqual(str(self.root), echo_sync.card_argument(args))
        for error in (EOFError(), KeyboardInterrupt(), None):
            with mock.patch("builtins.input", side_effect=error, return_value=""):
                echo_sync.wait_before_closing()
        for error in (EOFError(), KeyboardInterrupt()):
            with mock.patch.object(echo_sync, "run", side_effect=error), mock.patch.object(sys.stdin, "isatty", return_value=False), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(1, echo_sync.main([str(self.root)]))
        with mock.patch.object(echo_sync, "run", return_value=0), mock.patch.object(sys.stdin, "isatty", return_value=True), mock.patch.object(echo_sync, "wait_before_closing") as close:
            self.assertEqual(0, echo_sync.main([]))
            close.assert_called_once()

    def test_fingerprint_check_distinguishes_missing_changed_and_incomplete_records(self):
        fingerprint = echo_sync.Fingerprint(True, 1, 2, 3)
        snapshot = echo_sync.ScanSnapshot(fingerprint)
        drive = echo_sync.MusicDrive(self.root, echo_sync.card_volume("fat32"))
        drives = echo_sync.MusicDrives(drive)
        record = echo_sync.scan_record(snapshot, bytes(16))
        for data, current in ((b"", fingerprint), (record, echo_sync.Fingerprint(True, 2, 2, 3)), (record, echo_sync.Fingerprint(False, 1, 2, 3))):
            with mock.patch.object(echo_sync, "take_fingerprint", return_value=current), mock.patch.object(echo_sync, "read_prefix", return_value=data):
                self.assertEqual(1, echo_sync.run_check(drives))
        self.assertEqual("none", echo_sync.describe(None))
        self.assertEqual("incomplete", echo_sync.describe(echo_sync.Fingerprint(False, 0, 0, 0)))
        for after in (echo_sync.Fingerprint(False, 0, 0, 0), echo_sync.Fingerprint(True, 2, 2, 3)):
            with mock.patch.object(echo_sync, "take_fingerprint", return_value=after), mock.patch.object(echo_sync, "write_or_report") as write:
                self.assertFalse(echo_sync.record_fingerprint(
                    self.root, drives, snapshot, echo_sync.SyncOptions()))
                write.assert_not_called()
        with mock.patch.object(echo_sync, "card_fingerprint", side_effect=OSError("disconnected")):
            self.assertFalse(echo_sync.take_fingerprint(self.root, echo_sync.card_volume("fat32")).complete)


class PlayerDriveDiscoveryTest(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.card = self.root / "Card volume"
        self.internal = self.root / "Internal volume"

    def test_macos_uses_both_media_on_the_same_usb_player(self):
        def medium(name, *points):
            return {"_name": name, "volumes": [{"mount_point": point} for point in points]}

        def player(*media, pid="0x0001"):
            return {"_name": echo_sync.IDENTITY.name, "vendor_id": "0x1209 (pid.codes)",
                    "product_id": pid, "Media": list(media)}

        card = medium("SD card Media", None, str(self.card))
        internal = medium("Internal Storage Media", str(self.internal))
        elsewhere = medium("Internal Storage Media", str(self.root / "Elsewhere"))
        tree = {"SPUSBDataType": [{"_items": [player(card, internal, medium("Other")),
                                             player(elsewhere), player(card, pid="0x0002")]}]}
        with (mock.patch.object(echo_sync.sys, "platform", "darwin"),
              mock.patch.object(echo_sync.subprocess, "run",
                                return_value=SimpleNamespace(stdout=json.dumps(tree))) as run):
            self.assertEqual([(self.card, self.internal)], echo_sync.mounted_player_pairs())
        self.assertEqual(["system_profiler", "SPUSBDataType", "-json"], run.call_args.args[0])
        tree["SPUSBDataType"][0]["_items"] = [player(card), player(internal)]
        with (mock.patch.object(echo_sync.sys, "platform", "darwin"),
              mock.patch.object(echo_sync, "command_json", return_value=tree)):
            self.assertEqual([], echo_sync.mounted_player_pairs())

    def test_linux_uses_block_mounts_and_the_usb_parent_lun(self):
        block = self.root / "block"
        block.mkdir()
        player = self.root / "usb1" / "1-8"
        player.mkdir(parents=True)
        for field, value in (("idVendor", "1209"), ("idProduct", "0001"),
                             ("product", echo_sync.IDENTITY.name)):
            (player / field).write_text(value)
        for disk, lun in (("sda", 0), ("sdb1", 1), ("sdc", 2)):
            path = player / "host5" / f"5:0:0:{lun}" / "block" / disk
            path.mkdir(parents=True)
            (block / disk).symlink_to(path)
        self.assertEqual((player, 0), echo_sync.linux_player_lun("/dev/sda", block))
        self.assertEqual((player, 1), echo_sync.linux_player_lun("/dev/sdb1", block))
        self.assertIsNone(echo_sync.linux_player_lun("/dev/sdc", block))
        self.assertIsNone(echo_sync.linux_player_lun("/dev/absent", block))
        self.assertIsNone(echo_sync.linux_player_lun("tmpfs", block))
        local = self.root / "virtual" / "block" / "loop0"
        local.mkdir(parents=True)
        (block / "loop0").symlink_to(local)
        self.assertIsNone(echo_sync.linux_player_lun("/dev/loop0", block))
        (player / "product").write_text("Another USB device")
        self.assertIsNone(echo_sync.linux_player_lun("/dev/sda", block))

        rows = [("/dev/sda", str(self.internal), "vfat", ""),
                ("/dev/sdb1", str(self.card), "exfat", ""),
                ("/dev/missing", str(self.root / "missing"), "vfat", ""),
                ("tmpfs", str(self.root / "tmp"), "tmpfs", "")]
        with (mock.patch.object(echo_sync.sys, "platform", "linux"),
              mock.patch.object(echo_sync, "linux_mount_rows", return_value=rows),
              mock.patch.object(echo_sync, "linux_player_lun",
                                side_effect=[(player, 0), (player, 1), None]) as lun):
            self.assertEqual([(self.card, self.internal)], echo_sync.mounted_player_pairs())
        self.assertEqual(3, lun.call_count)

    def test_windows_does_not_mix_different_devices_or_ambiguous_luns(self):
        rows = [{"device": "player-1", "lun": 1, "root": str(self.card)},
                {"device": "player-1", "lun": 0, "root": str(self.internal)},
                {"device": "player-2", "lun": 0, "root": str(self.root / "Unpaired")},
                {"device": "player-1", "lun": 3, "root": str(self.root / "Different LUN")}]
        with (mock.patch.object(echo_sync.sys, "platform", "win32"),
              mock.patch.object(echo_sync, "command_json", return_value=rows) as query):
            self.assertEqual([(self.card, self.internal)], echo_sync.mounted_player_pairs())
            command = query.call_args.args[0]
            self.assertEqual("powershell.exe", command[0])
            self.assertIn("DEVPKEY_Device_Parent", command[-1])
            self.assertIn("SCSILogicalUnit", command[-1])
            rows.append({"device": "player-1", "lun": 1, "root": str(self.root / "Second partition")})
            self.assertEqual([], echo_sync.mounted_player_pairs())
            query.return_value = None
            self.assertEqual([], echo_sync.mounted_player_pairs())

    def test_probe_failure_and_unknown_platform_leave_single_drive_discovery_available(self):
        for error in (OSError("missing program"), subprocess.TimeoutExpired("system_profiler", 15)):
            with mock.patch.object(echo_sync.subprocess, "run", side_effect=error):
                self.assertEqual([], echo_sync.command_json(["system_profiler"]))
        with mock.patch.object(echo_sync.subprocess, "run",
                               return_value=SimpleNamespace(stdout="invalid JSON")):
            self.assertEqual([], echo_sync.command_json(["system_profiler"]))
        with mock.patch.object(echo_sync.sys, "platform", "other"):
            self.assertEqual([], echo_sync.mounted_player_pairs())


if __name__ == "__main__":
    unittest.main()
