import hashlib
import shutil
import struct
import sys
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import echo_debug
import echo_debug_mcp
import echo_first_install
import make_image
from test_echo_debug import FakeHost, VolumeCase, medium, player, usb_tree


class InstallIO:
    def __init__(self):
        self.events = []
        self.fail_allocation = False
        self.bad_read = 0
        self.reads = 0

    def allocate(self, stream, size):
        self.events.append("allocate")
        if self.fail_allocation:
            raise echo_debug.DebugError("contiguous_allocation_failed", "allocation refused")
        return size

    def digest(self, path):
        self.events.append("read")
        self.reads += 1
        return "bad" if self.reads == self.bad_read else hashlib.sha256(path.read_bytes()).hexdigest()

    def remount(self, volume):
        self.events.append("remount")
        return volume


class StockHost(FakeHost):
    def __init__(self, root, card, io):
        internal = medium("ECHO", "ECHO", root)
        internal["Logical Unit"] = 0
        external = medium("SD", "MUSIC", card, 32 << 30)
        external["Logical Unit"] = 1
        super().__init__(usb_tree(player([internal, external], "ECHO", "0x071b", "0x3203")))
        self.io = io

    def first_install_io(self):
        return self.io


class FirstInstall(VolumeCase):
    @classmethod
    def setUpClass(cls):
        vector = struct.pack("<II", make_image.DATA_BASE, 0x03050101)
        firmware = make_image.Firmware(vector + bytes(312), vector, 256)
        cls.data = make_image.build_image(firmware, echo_debug.IDENTITY["id"], "abc1234",
                                          (2026, 11, 13))

    def setUp(self):
        super().setUp()
        shutil.rmtree(self.root / echo_debug.DATA_DIRECTORY)
        self.image = Path(self.directory.name) / "candidate.img"
        self.image.write_bytes(self.data)
        self.io = InstallIO()
        self.stock = StockHost(self.root, self.card, self.io)

    def test_cli_prepares_stock_first_install_without_existing_data_folder(self):
        code, result = self.run_cli("first-install", str(self.image), host=self.stock)
        self.assertEqual(code, 0, result)
        self.assertEqual((self.root / "ECHOECHO.img").read_bytes(), self.data)
        self.assertEqual(self.io.events, ["allocate", "read", "remount", "read"])
        self.assertEqual(result["state"], "ready_for_restart")
        self.assertEqual(result["image_build"], "abc1234")
        self.assertEqual(result["sha256"], hashlib.sha256(self.data).hexdigest())
        self.assertEqual(self.stock.ejected, [str(self.root), str(self.card)])
        self.assertFalse((self.root / echo_debug.DATA_DIRECTORY).exists())

    def test_mcp_runs_the_same_first_install_operation(self):
        reply = echo_debug_mcp.call_tool(echo_debug.Tool(self.stock), "echo_first_install",
                                          {"image": str(self.image)})
        self.assertFalse(reply["isError"], reply)
        self.assertEqual(reply["structuredContent"]["state"], "ready_for_restart")
        self.assertEqual(self.io.events, ["allocate", "read", "remount", "read"])

    def test_allocation_failure_removes_only_the_owned_image_and_does_not_eject(self):
        keep = self.root / "music.mp3"
        keep.write_bytes(b"keep")
        self.io.fail_allocation = True
        code, result = self.run_cli("first-install", str(self.image), host=self.stock)
        self.assertEqual(code, 1)
        self.assertEqual(result["error"]["code"], "contiguous_allocation_failed")
        self.assertFalse((self.root / "ECHOECHO.img").exists())
        self.assertFalse(self.stock.ejected)
        self.assertEqual(keep.read_bytes(), b"keep")

    def test_mismatch_before_or_after_remount_removes_copy_without_ejecting(self):
        for bad_read in (1, 2):
            with self.subTest(read=bad_read):
                self.io.bad_read = bad_read
                self.io.reads = 0
                code, result = self.run_cli("first-install", str(self.image), host=self.stock)
                self.assertEqual(code, 1)
                self.assertEqual(result["error"]["code"], "copy_mismatch")
                self.assertFalse((self.root / "ECHOECHO.img").exists())
                self.assertFalse(self.stock.ejected)

    def test_other_update_image_is_preserved_and_blocks_preparation(self):
        existing = self.root / "ECHOV200.IMG"
        existing.write_bytes(b"owner image")
        code, result = self.run_cli("first-install", str(self.image), host=self.stock)
        self.assertEqual(code, 1)
        self.assertEqual(result["error"]["code"], "pending_update")
        self.assertEqual(existing.read_bytes(), b"owner image")
        self.assertFalse(self.io.events)
        self.assertFalse(self.stock.ejected)

    def test_mini_or_nonstock_device_is_refused(self):
        for name in ("ECHO MINI", echo_debug.IDENTITY["name"]):
            with self.subTest(name=name):
                device = self.stock.tree["SPUSBDataType"][0]["_items"][-1]
                device["_name"] = name
                code, result = self.run_cli("first-install", str(self.image), host=self.stock)
                self.assertEqual(code, 1)
                self.assertEqual(result["error"]["code"], "stock_player_not_connected")
                self.assertFalse(self.io.events)

    def test_crc_failure_is_refused_before_copying(self):
        changed = bytearray(self.data)
        changed[0x10000] ^= 1
        self.image.write_bytes(changed)
        code, result = self.run_cli("first-install", str(self.image), host=self.stock)
        self.assertEqual(code, 1)
        self.assertEqual(result["error"]["code"], "not_an_image")
        self.assertFalse(self.io.events)
        self.assertFalse(self.stock.ejected)

    def test_volume_override_cannot_bypass_stock_usb_identification(self):
        code, result = self.run_cli("--volume", str(self.root), "first-install", str(self.image), host=self.stock)
        self.assertEqual(code, 1)
        self.assertEqual(result["error"]["code"], "usb_required")
        self.assertFalse(self.io.events)

    def test_macos_allocator_requires_the_whole_file_to_be_contiguous(self):
        with mock.patch.object(sys, "platform", "darwin"), mock.patch("fcntl.fcntl") as allocate:
            allocate.return_value = struct.pack("=IIqqq", 6, 3, 0, len(self.data), len(self.data))
            with self.image.open("rb") as stream:
                self.assertEqual(echo_first_install.MacInstallIO().allocate(stream, len(self.data)), len(self.data))
            request = struct.unpack("=IIqqq", allocate.call_args.args[2])
            self.assertEqual(request, (6, 3, 0, len(self.data), 0))
            self.assertEqual(allocate.call_args.args[1], 42)

    def test_partial_contiguous_allocation_is_refused(self):
        with mock.patch.object(sys, "platform", "darwin"), mock.patch("fcntl.fcntl") as allocate:
            allocate.return_value = struct.pack("=IIqqq", 6, 3, 0, len(self.data), 4096)
            with self.image.open("rb") as stream:
                with self.assertRaises(echo_debug.DebugError) as raised:
                    echo_first_install.MacInstallIO().allocate(stream, len(self.data))
            self.assertEqual(raised.exception.code, "contiguous_allocation_failed")
            self.assertEqual(allocate.call_count, 1)

    def test_macos_readback_requests_uncached_io(self):
        with mock.patch.object(sys, "platform", "darwin"), mock.patch("fcntl.fcntl") as uncached:
            digest = echo_first_install.MacInstallIO().digest(self.image)
            self.assertEqual(digest, hashlib.sha256(self.data).hexdigest())
            self.assertEqual(uncached.call_args.args[1:], (48, 1))
