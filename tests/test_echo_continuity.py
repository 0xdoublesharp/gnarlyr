import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import echo_continuity
import echo_debug
import echo_debug_mcp
from test_echo_debug import FakeHost, VolumeCase, medium, player, usb_tree

DATA = echo_debug.DATA_DIRECTORY


def list_bytes(builds, newest):
    body = b"DSBU" + bytes([1, newest, 0, 0])
    for build in builds:
        body += build.encode("ascii").ljust(16, b"\0")
    return body + struct.pack("<I", echo_debug.fnv1a(body))


def mark_for(build, request=0):
    name = build.encode("ascii")
    return struct.pack("<I", echo_debug.fnv1a(name)) + bytes([1, request, 0, 0]) + name.ljust(16, b"\0")


class Catalogue(unittest.TestCase):
    def test_the_tool_lists_the_same_files_as_the_firmware(self):
        source = (ROOT / "firmware" / "src" / "settings" / "user_state.c").read_text(encoding="utf-8")
        table = source[source.index("static const UserStateKind FILES[]"):source.index("static const UserStateKind TAPE")]
        firmware = re.findall(r'"([a-z_\-]+\.bin)", USER_STATE_(INTERNAL|MUSIC)', table)
        tool = [(name, volume) for name, volume in echo_continuity.catalogue() if name not in echo_continuity.TAPE_FILES]
        self.assertEqual([(name, volume.lower()) for name, volume in firmware], tool)
        self.assertIn("USER_STATE_TAPES = %d" % echo_continuity.TAPES,
                      (ROOT / "firmware" / "src" / "settings" / "include" / "settings" / "user_state.h").read_text())
        self.assertEqual(("mix00a.bin", "music"), echo_continuity.catalogue()[len(tool)])
        self.assertEqual(("mix31b.bin", "music"), echo_continuity.catalogue()[-1])

    def test_the_list_reads_newest_first_and_a_damaged_list_reads_as_empty(self):
        data = list_bytes(["build-one", "build-two"], 1)
        backups = echo_continuity.decode_list(data, echo_debug.fnv1a)
        self.assertEqual(["build-two", "build-one"], [backup["build"] for backup in backups])
        self.assertEqual(["backup/2", "backup/1"], [backup["folder"] for backup in backups])
        self.assertTrue(backups[0]["newest"])
        damaged = bytearray(data)
        damaged[10] ^= 1
        self.assertEqual([], echo_continuity.decode_list(bytes(damaged), echo_debug.fnv1a))
        self.assertEqual(["build-one"], [backup["build"] for backup in
                                         echo_continuity.decode_list(list_bytes(["build-one", ""], 1),
                                                                     echo_debug.fnv1a)])

    def test_a_restore_request_keeps_the_recorded_build(self):
        mark = echo_continuity.mark_bytes(2, mark_for("1.0.0-aaaa1111"), echo_debug.fnv1a)
        self.assertEqual(mark_for("1.0.0-aaaa1111", 2), mark)
        fresh = echo_continuity.mark_bytes(1, b"", echo_debug.fnv1a)
        self.assertEqual(mark_for("", 1), fresh)


class Commands(VolumeCase):
    def setUp(self):
        super().setUp()
        self.data = self.root / DATA
        (self.data / "settings.bin").write_bytes(b"DSST" + bytes(74))
        (self.card / DATA / "listens.bin").write_bytes(b"DSLL" + bytes(60))
        (self.data / "backup" / "1").mkdir(parents=True)
        (self.data / "backup" / "1" / "settings.bin").write_bytes(b"old settings")
        (self.data / "backup" / "1" / "mix03b.bin").write_bytes(b"tape")
        (self.data / "backup" / "list.bin").write_bytes(list_bytes(["1.0.0-aaaa1111", ""], 0))
        (self.data / "continuity.bin").write_bytes(mark_for("1.0.0-bbbb2222"))

    def test_settings_backup_lists_what_the_player_kept(self):
        status, output = self.run_cli("--volume", str(self.root), "settings-backup", host=FakeHost(None))
        self.assertEqual(0, status)
        self.assertEqual(1, len(output["backups"]))
        self.assertEqual("1.0.0-aaaa1111", output["backups"][0]["build"])
        self.assertEqual(["mix03b.bin", "settings.bin"], output["backups"][0]["files"])
        self.assertIsNone(output["restore_pending"])

    def test_mcp_backup_and_restore_use_the_same_preserved_files_and_request(self):
        host = self.host()
        tool = echo_debug.Tool(host)
        listed = echo_debug_mcp.call_tool(tool, "echo_settings_backup", {})
        self.assertFalse(listed["isError"])
        self.assertEqual("1.0.0-aaaa1111", listed["structuredContent"]["backups"][0]["build"])
        restored = echo_debug_mcp.call_tool(tool, "echo_settings_restore", {"backup": "newest"})
        self.assertFalse(restored["isError"])
        self.assertEqual(mark_for("1.0.0-bbbb2222", 1), (self.data / "continuity.bin").read_bytes())
        self.assertIn(str(self.root), host.ejected)

    def test_settings_backup_copies_the_backups_and_current_files_off_the_player(self):
        out = Path(self.directory.name) / "copies"
        tool = echo_debug.Tool(self.host())
        result = tool.settings_backup(out)
        self.assertEqual(["settings.bin", "listens.bin"], result["current"])
        self.assertEqual(b"DSLL" + bytes(60), (out / "current" / "listens.bin").read_bytes())
        self.assertEqual(b"old settings", (out / "backup-1" / "settings.bin").read_bytes())
        self.assertTrue((out / "manifest.json").is_file())

    def test_settings_backup_refuses_to_copy_onto_the_player(self):
        status, output = self.run_cli("--volume", str(self.root), "settings-backup", "--out",
                                      str(self.data / "copies"), host=FakeHost(None))
        self.assertEqual(1, status)
        self.assertEqual("write_refused", output["error"]["code"])

    def test_settings_restore_leaves_a_request_and_ejects(self):
        host = self.host()
        result = echo_debug.Tool(host).settings_restore("newest")
        self.assertEqual(mark_for("1.0.0-bbbb2222", 1), (self.data / "continuity.bin").read_bytes())
        self.assertEqual("1.0.0-aaaa1111", result["restore"]["build"])
        self.assertIn(str(self.root), host.ejected)
        self.assertIn(str(self.card), host.ejected)
        listed = echo_debug.Tool(FakeHost(None), volume=self.root).settings_backup()
        self.assertEqual(1, listed["restore_pending"])

    def test_settings_restore_accepts_a_build_name_and_refuses_an_unknown_one(self):
        tool = echo_debug.Tool(FakeHost(None), volume=self.root)
        self.assertEqual(0, tool.settings_restore("1.0.0-aaaa1111")["restore"]["slot"])
        with self.assertRaises(echo_debug.DebugError) as raised:
            tool.settings_restore("older")
        self.assertEqual("backup_missing", raised.exception.code)

    def test_the_debug_tool_still_refuses_other_files_in_the_data_folder(self):
        tool = echo_debug.Tool(FakeHost(None), volume=self.root)
        with self.assertRaises(echo_debug.DebugError) as raised:
            tool.write(self.root, self.data / "settings.bin", b"x")
        self.assertEqual("write_refused", raised.exception.code)

    def test_a_player_with_two_cards_is_refused(self):
        host = FakeHost(usb_tree(player([medium("Internal storage", "ECHO", self.root),
                                         medium("SD card", "MUSIC", self.card, 32 << 30),
                                         medium("SD card", "OTHER", self.card, 32 << 30)])))
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.Tool(host).settings_backup()
        self.assertEqual("continuity_volume_ambiguous", raised.exception.code)


if __name__ == "__main__":
    unittest.main()
