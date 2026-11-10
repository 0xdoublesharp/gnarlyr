import hashlib
import io
import json
import struct
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import echo_debug
from test_echo_debug import SYSTEM_LINES, FakeHost, VolumeCase, medium, player, report_text, usb_tree

IMAGE_BUILD = "abc1234"
SCRIPT = "play,down\nmeasure 2000\nscreenshot home\nreconnect\n"
RED, GREEN, BLUE, WHITE = 0xF800, 0x07E0, 0x001F, 0xFFFF


def image(brand=b"DBLSHARP", build=IMAGE_BUILD, size=0x2000):
    data = bytearray(size)
    data[0x10:0x10 + len(brand)] = brand
    data[0x30:0x30 + len(build)] = build.encode("ascii")
    data[0x100:0x200] = bytes(range(256))
    return bytes(data)


def script_report(script, state="finished", build=IMAGE_BUILD, screenshots=("home",)):
    lines = [line.replace("1051212", build) for line in SYSTEM_LINES]
    lines += ["script.state: " + state, "script.started_ms: 5000", "script.finished_ms: 9000",
              "script.interrupted_line: 0"]
    lines += ["script.line: %d ok %s" % (number + 1, text)
              for number, text in enumerate(echo_debug.echoed_lines(script.encode("utf-8")))]
    lines += ["script.measure: line=3 ms=2000 frames=90 frame_avg_us=21000 audio=1 underruns=0 "
              "handoff_avg_us=unknown core0_busy_percent=37"]
    lines += ["script.screenshot: %d %s" % (slot, name) for slot, name in enumerate(screenshots)]
    return report_text(lines)


def screenshot(name, width=4, height=2, pixels=None, magic=b"DSSH"):
    pixels = pixels or [RED, GREEN, BLUE, WHITE] * 2
    header = echo_debug.SCREENSHOT_HEADER.pack(magic, 1, 1, width, height, 32, 12345, name.encode("ascii"))
    return header + struct.pack("<%dH" % len(pixels), *pixels)


def png_pixels(data):
    assert data[:8] == echo_debug.PNG_SIGNATURE
    chunks, offset = {}, 8
    while offset < len(data):
        length, kind = struct.unpack_from(">I4s", data, offset)
        body = data[offset + 8:offset + 8 + length]
        assert struct.unpack_from(">I", data, offset + 8 + length)[0] == zlib.crc32(kind + body)
        chunks[kind] = chunks.get(kind, b"") + body
        offset += 12 + length
    width, height, depth, colour = struct.unpack_from(">IIBB", chunks[b"IHDR"])
    rows = zlib.decompress(chunks[b"IDAT"])
    assert (depth, colour) == (8, 2) and len(rows) == height * (1 + 3 * width)
    return width, height, [tuple(rows[y * (1 + 3 * width) + 1 + 3 * x:][:3])
                           for y in range(height) for x in range(width)]


class Clock:
    def __init__(self):
        self.now = 0.0

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class PlayerHost(FakeHost):
    """A computer with a player on it: once every drive is ejected the player goes away,
    acts on what was written to its drive, and comes back by itself a few polls later."""

    def __init__(self, case, act=None, returns=True, leaves=True):
        super().__init__(None)
        self.case = case
        self.act = act
        self.returns = returns
        self.leaves = leaves
        self.away_polls = None

    def connected_tree(self):
        return usb_tree(player([medium("Internal storage", "ECHO", self.case.root),
                                medium("SD card", "MUSIC", self.case.card, 32 << 30)]))

    def usb_tree(self):
        if self.away_polls is None:
            return self.connected_tree()
        self.away_polls += 1
        if self.returns and self.away_polls > 3:
            self.away_polls = None
            if self.act:
                self.act()
            return self.connected_tree()
        return usb_tree()

    def eject(self, mount_point):
        super().eject(mount_point)
        if self.leaves and len(self.ejected) == 2:
            self.away_polls = 0


class LoopCase(VolumeCase):
    def setUp(self):
        super().setUp()
        self.clock = Clock()
        self.image = Path(self.directory.name) / "build.img"
        self.image.write_bytes(image())
        self.script = Path(self.directory.name) / "check.txt"
        self.script.write_text(SCRIPT, encoding="utf-8")
        self.write_report()

    def tool(self, host, **options):
        return echo_debug.Tool(host, sleep=self.clock.sleep, clock=self.clock.time, **options)

    def files(self, root):
        return sorted(str(path.relative_to(root)) for path in root.rglob("*") if path.is_file())

    def assert_refused(self, code, call):
        with self.assertRaises(echo_debug.DebugError) as raised:
            call()
        self.assertEqual(raised.exception.code, code)


class Installing(LoopCase):
    def test_the_image_is_copied_as_the_update_file_verified_and_both_drives_ejected(self):
        host = PlayerHost(self)
        result = self.tool(host).install(self.image)
        copy = self.root / "DBLSHARP.IMG"
        self.assertEqual(copy.read_bytes(), image())
        self.assertEqual(result["sha256"], hashlib.sha256(image()).hexdigest())
        self.assertEqual((result["bytes"], result["image_build"]), (len(image()), IMAGE_BUILD))
        self.assertEqual(result["update_file"], str(copy))
        self.assertEqual(host.ejected, [str(self.root), str(self.card)])
        self.assertEqual((result["restart_requested"], result["waited"]), (False, False))

    def test_nothing_else_is_written_and_the_card_is_not_touched(self):
        before = self.files(self.card)
        self.tool(PlayerHost(self)).install(self.image)
        self.assertEqual(self.files(self.root), [".doublesharp/debug/report0.txt", "DBLSHARP.IMG"])
        self.assertEqual(self.files(self.card), before)

    def test_a_file_that_is_not_this_players_image_is_refused_before_anything_is_written(self):
        host = PlayerHost(self)
        for data in (image(brand=b"Rockchip"), image(brand=b"EchoPod"), b"", image(size=0x800)):
            self.image.write_bytes(data)
            self.assert_refused("not_an_image", lambda: self.tool(host).install(self.image))
        self.assert_refused("image_missing", lambda: self.tool(host).install(self.root / "none.img"))
        self.assertFalse((self.root / "DBLSHARP.IMG").exists())
        self.assertEqual(host.ejected, [])

    def test_without_a_player_nothing_is_written(self):
        self.assert_refused("player_not_connected",
                            lambda: self.tool(FakeHost(usb_tree())).install(self.image))
        self.assertFalse((self.root / "DBLSHARP.IMG").exists())

    def test_a_wrong_volume_is_refused_before_anything_is_written(self):
        host = FakeHost(usb_tree(player([medium("Internal storage", "UNTITLED", self.root)])))
        self.assert_refused("wrong_volume_name", lambda: self.tool(host).install(self.image))
        host = FakeHost(usb_tree(player([medium("SD card", "ECHO", self.root)])))
        self.assert_refused("internal_drive_not_identified", lambda: self.tool(host).install(self.image))
        self.assertFalse((self.root / "DBLSHARP.IMG").exists())

    def test_restart_writes_the_request_and_waits_for_the_new_build(self):
        def restarted():
            self.assertEqual((self.debug / "restart.req").read_bytes(), b"RESTART AFTER INSTALL\n")
            (self.debug / "restart.req").unlink()
            self.write_report(report_text([line.replace("1051212", IMAGE_BUILD) for line in SYSTEM_LINES]))

        result = self.tool(PlayerHost(self, restarted)).install(self.image, restart=True)
        self.assertEqual((result["restart_requested"], result["waited"]), (True, True))
        self.assertEqual((result["running_build"], result["running_the_image"]), (IMAGE_BUILD, True))

    def test_a_player_that_comes_back_on_the_old_build_is_reported_as_such(self):
        result = self.tool(PlayerHost(self)).install(self.image, restart=True)
        self.assertEqual((result["running_build"], result["running_the_image"]), ("1051212", False))

    def test_a_player_that_does_not_come_back_is_an_error_after_the_timeout(self):
        tool = self.tool(PlayerHost(self, returns=False))
        self.assert_refused("player_did_not_return", lambda: tool.install(self.image, True, timeout=30))
        self.assertGreaterEqual(self.clock.now, 30)
        self.assertLess(self.clock.now, 100)

    def test_a_player_that_never_leaves_is_an_error(self):
        tool = self.tool(PlayerHost(self, leaves=False))
        self.assert_refused("player_did_not_leave", lambda: tool.install(self.image, True))

    def test_with_a_named_folder_nothing_is_ejected_or_waited_for(self):
        host = FakeHost(None)
        result = self.tool(host, volume=self.root).install(self.image, restart=True)
        self.assertEqual((result["ejected"], result["waited"]), ([], False))
        self.assertTrue((self.debug / "restart.req").is_file())
        self.assertEqual(host.ejected, [])


class Waiting(LoopCase):
    def test_wait_returns_the_status_once_the_player_is_back(self):
        host = PlayerHost(self)
        host.away_polls = 0
        status = self.tool(host).wait(timeout=30)
        self.assertTrue(status["connected"])
        self.assertEqual(status["newest_report"]["file"], "report0.txt")
        self.assertGreater(self.clock.now, 0)

    def test_wait_gives_up_after_its_timeout(self):
        tool = self.tool(PlayerHost(self, returns=False))
        tool.host.away_polls = 0
        self.assert_refused("player_did_not_return", lambda: tool.wait(timeout=10))
        self.assertGreaterEqual(self.clock.now, 10)

    def test_wait_treats_a_drive_that_is_not_mounted_yet_as_not_there(self):
        unmounted = medium("Internal storage", "ECHO", self.root)
        unmounted["volumes"][0]["mount_point"] = None
        tool = self.tool(FakeHost(usb_tree(player([unmounted]))))
        self.assert_refused("player_did_not_return", lambda: tool.wait(timeout=5))


class WriteGuard(LoopCase):
    def test_only_the_update_file_and_the_debug_folder_can_be_written(self):
        tool = self.tool(PlayerHost(self))
        for path in (self.root / "notes.txt", self.root / ".doublesharp" / "settings.bin",
                     self.root / "Music" / "DBLSHARP.IMG", self.debug / "deeper" / "run.txt",
                     self.card / "DBLSHARP.IMG", self.card / ".doublesharp" / "debug" / "run.txt",
                     self.debug / ".." / ".." / "escape.txt"):
            self.assert_refused("write_refused", lambda: tool.write(self.root, path, b"x"))
        self.assertEqual(self.files(self.root), [".doublesharp/debug/report0.txt"])
        self.assertEqual(self.files(self.card), [])

    def test_a_link_out_of_the_debug_folder_is_not_followed(self):
        outside = Path(self.directory.name) / "outside.txt"
        outside.write_text("keep")
        (self.debug / "run.txt").symlink_to(outside)
        tool = self.tool(PlayerHost(self))
        self.assert_refused("write_refused", lambda: tool.write(self.root, self.debug / "run.txt", b"x"))
        self.assertEqual(outside.read_text(), "keep")

    def test_a_volume_without_the_data_folder_is_not_written(self):
        other = Path(self.directory.name) / "other"
        other.mkdir()
        tool = self.tool(PlayerHost(self))
        self.assert_refused("not_a_player_volume",
                            lambda: tool.write(other, other / "DBLSHARP.IMG", b"x"))
        self.assertEqual(self.files(other), [])


class Running(LoopCase):
    def player_runs(self, **report):
        def act():
            sent = (self.debug / "run.txt").read_text(encoding="utf-8")
            (self.debug / "run.txt").unlink()
            self.sent = sent
            self.write_report(script_report(sent, **report))

        return PlayerHost(self, act)

    def test_a_script_is_sent_with_the_running_build_ejected_and_its_results_collected(self):
        host = self.player_runs()
        result = self.tool(host).run(self.script)
        self.assertEqual(self.sent, "build 1051212\n" + SCRIPT)
        self.assertEqual(result["sent"], self.sent)
        self.assertEqual(host.ejected, [str(self.root), str(self.card)])
        self.assertTrue(result["waited"])
        script = result["script"]
        self.assertEqual(script["state"], "finished")
        self.assertEqual([line["status"] for line in script["lines"]], ["ok"] * 5)
        self.assertEqual(script["lines"][1], {"line": 2, "status": "ok", "text": "play,down"})
        self.assertEqual(script["measures"][0]["frames"], 90)
        self.assertIsNone(script["measures"][0]["handoff_avg_us"])
        self.assertEqual(script["screenshots"], [{"slot": 0, "name": "home"}])
        self.assertEqual((script["started_ms"], script["finished_ms"]), (5000, 9000))

    def test_a_script_that_names_its_build_is_sent_as_it_is(self):
        self.script.write_text("# nightly\r\nbuild any\r\nplay", encoding="utf-8")
        self.tool(self.player_runs()).run(self.script)
        self.assertEqual(self.sent, "# nightly\nbuild any\nplay\n")

    def test_a_script_too_long_for_the_player_is_refused_before_anything_is_written(self):
        host = self.player_runs()
        self.script.write_text("build any\n" + "wait 100\n" * 40, encoding="utf-8")
        self.assert_refused("script_too_long", lambda: self.tool(host).run(self.script))
        self.script.write_text("build any\n# " + "x" * 640 + "\n", encoding="utf-8")
        self.assert_refused("script_too_long", lambda: self.tool(host).run(self.script))
        self.assertFalse((self.debug / "run.txt").exists())
        self.assertEqual(host.ejected, [])

    def test_without_a_build_line_and_without_a_known_build_the_script_is_not_sent(self):
        self.write_report(report_text([line.replace("1051212", "unknown") if "firmware.build" in line
                                       else line for line in SYSTEM_LINES]))
        self.assert_refused("build_unknown", lambda: self.tool(self.player_runs()).run(self.script))
        self.assertFalse((self.debug / "run.txt").exists())

    def test_a_refused_script_is_an_error_naming_the_players_reason(self):
        tool = self.tool(self.player_runs(state="refused_build"))
        self.assert_refused("script_refused_build", lambda: tool.run(self.script))

    def test_a_report_of_another_script_is_not_taken_for_this_run(self):
        host = PlayerHost(self, lambda: self.write_report(script_report("build 1051212\nplay\n")))
        self.assert_refused("stale_report", lambda: self.tool(host).run(self.script))

    def test_a_report_without_a_script_section_is_not_taken_for_this_run(self):
        self.assert_refused("stale_report", lambda: self.tool(PlayerHost(self)).run(self.script))

    def test_text_outside_ascii_is_compared_as_the_player_echoes_it(self):
        self.script.write_text("build any\nset Skip songs=Hold vol±\n", encoding="utf-8")
        result = self.tool(self.player_runs()).run(self.script)
        self.assertEqual(result["script"]["lines"][1]["text"], "set Skip songs=Hold vol??")

    def test_a_missing_script_file_is_an_error(self):
        self.assert_refused("script_missing", lambda: self.tool(PlayerHost(self)).run(self.root / "no.txt"))

    def test_with_a_named_folder_the_script_is_only_copied(self):
        result = self.tool(FakeHost(None), volume=self.root).run(self.script)
        self.assertEqual((result["waited"], result["ejected"]), (False, []))
        self.assertEqual((self.debug / "run.txt").read_text(), "build 1051212\n" + SCRIPT)

    def test_the_card_is_only_ejected_never_read_or_written(self):
        (self.card / ".doublesharp" / "debug" / "run.txt").write_text("old")
        before = {path: path.read_bytes() for path in self.card.rglob("*") if path.is_file()}
        self.tool(self.player_runs()).run(self.script)
        self.assertEqual({path: path.read_bytes() for path in self.card.rglob("*") if path.is_file()}, before)


class Screenshots(LoopCase):
    def setUp(self):
        super().setUp()
        self.out = Path(self.directory.name) / "png"
        self.write_report(script_report("build any\n" + SCRIPT))
        (self.debug / "shot0.565").write_bytes(screenshot("home"))

    def test_rgb565_widens_to_full_range_rgb(self):
        rgb = echo_debug.rgb888(struct.pack("<5H", RED, GREEN, BLUE, WHITE, 0), 5)
        self.assertEqual(rgb, bytes((255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0)))

    def test_a_listed_screenshot_becomes_a_png_named_after_it(self):
        result = self.tool(PlayerHost(self)).screenshots(self.out)
        shot = result["screenshots"][0]
        self.assertEqual((shot["name"], shot["slot"], shot["width"], shot["height"], shot["uptime_ms"]),
                         ("home", 0, 4, 2, 12345))
        self.assertEqual(shot["png"], str(self.out.resolve() / "home.png"))
        width, height, pixels = png_pixels(Path(shot["png"]).read_bytes())
        self.assertEqual((width, height), (4, 2))
        self.assertEqual(pixels, [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)] * 2)

    def test_a_full_frame_converts(self):
        pixels = [(x * 7 + y * 13) & 0xFFFF for y in range(222) for x in range(480)]
        (self.debug / "shot0.565").write_bytes(screenshot("home", 480, 222, pixels))
        self.tool(PlayerHost(self)).screenshots(self.out)
        width, height, converted = png_pixels((self.out / "home.png").read_bytes())
        self.assertEqual((width, height, len(converted)), (480, 222, 480 * 222))

    def test_without_screenshots_in_the_report_nothing_is_converted(self):
        self.write_report(script_report("build any\nplay\n", screenshots=()))
        self.assertEqual(self.tool(PlayerHost(self)).screenshots(self.out)["screenshots"], [])

    def test_a_file_left_from_another_run_is_not_passed_off_as_this_one(self):
        (self.debug / "shot0.565").write_bytes(screenshot("older"))
        self.assert_refused("stale_screenshot", lambda: self.tool(PlayerHost(self)).screenshots(self.out))

    def test_an_unfinished_or_impossible_screenshot_is_refused(self):
        tool = self.tool(PlayerHost(self))
        for data in (bytes(4096), screenshot("home", 4, 2, [0] * 7), screenshot("home", 481, 2, [0] * 962),
                     screenshot("home", magic=b"DSSX"), b"DSSH"):
            (self.debug / "shot0.565").write_bytes(data)
            self.assert_refused("bad_screenshot", lambda: tool.screenshots(self.out))
        (self.debug / "shot0.565").unlink()
        self.assert_refused("screenshot_missing", lambda: tool.screenshots(self.out))

    def test_png_files_never_go_onto_the_player(self):
        tool = self.tool(PlayerHost(self))
        self.assert_refused("write_refused", lambda: tool.screenshots(self.root / "png"))
        self.assert_refused("write_refused", lambda: tool.screenshots(self.debug))
        self.assertEqual(self.files(self.root), [".doublesharp/debug/report0.txt", ".doublesharp/debug/shot0.565"])


class CommandLine(LoopCase):
    def test_install_run_and_screenshots_print_json(self):
        root = ["--volume", str(self.root)]
        status, output = self.run_cli(*root, "install", str(self.image), host=FakeHost(None))
        self.assertEqual((status, output["ok"], output["image_build"]), (0, True, IMAGE_BUILD))
        status, output = self.run_cli(*root, "run", str(self.script), host=FakeHost(None))
        self.assertEqual((status, output["ok"], output["waited"]), (0, True, False))
        status, output = self.run_cli(*root, "screenshots", "--out", str(self.root), host=FakeHost(None))
        self.assertEqual((status, output["error"]["code"]), (1, "write_refused"))


if __name__ == "__main__":
    unittest.main()
