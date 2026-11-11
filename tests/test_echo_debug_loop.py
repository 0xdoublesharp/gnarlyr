import hashlib
import io
import json
import re
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
DEBUG = echo_debug.DATA_DIRECTORY + "/debug"
TIME_PATH = echo_debug.DATA_DIRECTORY + "/" + echo_debug.TIME_FILE
NOVEMBER_2026 = 1794482460
SCRIPT = "play,down\nmeasure 2000\nscreenshot home\nreconnect\n"
RED, GREEN, BLUE, WHITE = 0xF800, 0x07E0, 0x001F, 0xFFFF


def image(brand=echo_debug.IDENTITY["id"].encode("ascii"), build=IMAGE_BUILD, size=0x2000):
    data = bytearray(size)
    data[0x10:0x10 + len(brand)] = brand
    data[0x30:0x30 + len(build)] = build.encode("ascii")
    data[0x100:0x200] = bytes(range(256))
    return bytes(data)


SOAK_LINE = ("script.soak: line=4 minutes=30 end=done samples=20 first_mv=4012 last_mv=3950 "
             "mv_per_hour=-142 busy_percent=12 cable_start=1 charge_pin_start=1 supply_off_start=1 "
             "audio_start=1 screen_start=0 clock_mhz_start=200 cable_end=1 charge_pin_end=unknown "
             "supply_off_end=1 audio_end=1 screen_end=0 clock_mhz_end=200")


def script_report(script, state="finished", build=IMAGE_BUILD, screenshots=("home",), probes=(),
                  soaks=()):
    lines = [line.replace("1051212", build) for line in SYSTEM_LINES]
    lines += ["script.state: " + state, "script.started_ms: 5000", "script.finished_ms: 9000",
              "script.interrupted_line: 0"]
    lines += ["script.line: %d ok %s" % (number + 1, text)
              for number, text in enumerate(echo_debug.echoed_lines(script.encode("utf-8")))]
    lines += ["script.measure: line=3 ms=2000 frames=90 frame_avg_us=21000 audio=1 underruns=0 "
              "handoff_avg_us=unknown core0_busy_percent=37"]
    lines += ["script.screenshot: %d %s" % (slot, name) for slot, name in enumerate(screenshots)]
    lines += ["script.probe: line=%d ms=%d before=%s after=%s cable=%s charge_pin=%s" % probe
              for probe in probes]
    lines += list(soaks)
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


class SendingTheTime(LoopCase):
    def tool(self, host, **options):
        return super().tool(host, wall=lambda: (NOVEMBER_2026, -480), **options)

    def test_the_time_file_holds_the_utc_time_and_offset_sealed_like_the_firmware_reads_it(self):
        data = echo_debug.time_file_bytes(NOVEMBER_2026, -480)
        self.assertEqual(len(data), 32)
        self.assertEqual(data[:4], b"DSTM")
        self.assertEqual(struct.unpack_from("<HhI", data, 4), (1, -480, NOVEMBER_2026))
        self.assertEqual(data[12:28], bytes(16))
        self.assertEqual(struct.unpack_from("<I", data, 28)[0], echo_debug.fnv1a(data[:28]))

    def test_the_time_file_matches_the_firmware_format(self):
        firmware = echo_debug.ROOT / "firmware" / "platform" / "device" / "logic"
        header = (firmware / "include" / "device" / "time_file.h").read_text(encoding="utf-8")
        source = (firmware / "time_file.c").read_text(encoding="utf-8")
        values = dict(re.findall(r"(TIME_FILE_\w+) = (-?\d+)", header))
        values.update(re.findall(r"#define (TIME_FILE_\w+) UINT32_C\((\d+)\)", header))
        self.assertEqual(int(values["TIME_FILE_BYTES"]), len(echo_debug.time_file_bytes(NOVEMBER_2026, 0)))
        self.assertEqual(int(values["TIME_FILE_VERSION"]), echo_debug.TIME_VERSION)
        self.assertEqual((int(values["TIME_FILE_EARLIEST_UTC"]), int(values["TIME_FILE_LATEST_UTC"])),
                         (echo_debug.TIME_EARLIEST, echo_debug.TIME_LATEST))
        self.assertEqual(range(int(values["TIME_FILE_OFFSET_WEST"]), int(values["TIME_FILE_OFFSET_EAST"]) + 1),
                         echo_debug.TIME_OFFSET_MINUTES)
        self.assertIn('"/time.bin"', source)
        self.assertIn("{'D', 'S', 'T', 'M'}", source)

    def test_a_clock_the_player_would_refuse_is_never_written(self):
        for utc, offset in ((echo_debug.TIME_EARLIEST - 1, 0), (echo_debug.TIME_LATEST + 1, 0),
                            (NOVEMBER_2026, -721), (NOVEMBER_2026, 841)):
            with self.subTest(utc=utc, offset=offset):
                self.assert_refused("clock_out_of_range", lambda: echo_debug.time_file_bytes(utc, offset))

    def test_every_eject_sends_the_time_just_before_it(self):
        host = PlayerHost(self)
        result = self.tool(host).install(self.image)
        self.assertEqual((self.root / TIME_PATH).read_bytes(), echo_debug.time_file_bytes(NOVEMBER_2026, -480))
        self.assertEqual(host.ejected, [str(self.root), str(self.card)])
        self.assertTrue(result["ok"])

    def test_set_time_sends_the_time_and_ejects_both_drives(self):
        host = PlayerHost(self)
        result = self.tool(host).set_time()
        self.assertEqual((result["utc"], result["offset_minutes"]), (NOVEMBER_2026, -480))
        self.assertEqual(result["time_file"], str((self.root / TIME_PATH).resolve()))
        self.assertEqual(result["ejected"], [str(self.root), str(self.card)])
        self.assertEqual(self.files(self.root), [DEBUG + "/report0.txt", TIME_PATH])

    def test_set_time_refuses_a_bad_computer_clock_before_writing_or_ejecting(self):
        host = PlayerHost(self)
        tool = echo_debug.Tool(host, sleep=self.clock.sleep, clock=self.clock.time, wall=lambda: (0, 0))
        self.assert_refused("clock_out_of_range", tool.set_time)
        self.assertFalse((self.root / TIME_PATH).exists())
        self.assertEqual(host.ejected, [])

    def test_other_commands_go_on_without_the_time_when_the_computer_clock_is_wrong(self):
        host = PlayerHost(self)
        tool = echo_debug.Tool(host, sleep=self.clock.sleep, clock=self.clock.time, wall=lambda: (0, 0))
        self.assertTrue(tool.install(self.image)["ok"])
        self.assertFalse((self.root / TIME_PATH).exists())
        self.assertIn("time_skipped", tool.sent_time)
        self.assertEqual(host.ejected, [str(self.root), str(self.card)])

    def test_with_a_named_volume_set_time_writes_the_file_without_ejecting(self):
        tool = echo_debug.Tool(volume=self.root, wall=lambda: (NOVEMBER_2026, 60))
        result = tool.set_time()
        self.assertEqual(result["ejected"], [])
        self.assertEqual((self.root / TIME_PATH).read_bytes(), echo_debug.time_file_bytes(NOVEMBER_2026, 60))


class Installing(LoopCase):
    def test_the_image_is_copied_as_the_update_file_verified_and_both_drives_ejected(self):
        host = PlayerHost(self)
        result = self.tool(host).install(self.image)
        copy = self.root / echo_debug.UPDATE_FILE
        self.assertEqual(copy.read_bytes(), image())
        self.assertEqual(result["sha256"], hashlib.sha256(image()).hexdigest())
        self.assertEqual((result["bytes"], result["image_build"]), (len(image()), IMAGE_BUILD))
        self.assertEqual(result["update_file"], str(copy))
        self.assertEqual(host.ejected, [str(self.root), str(self.card)])
        self.assertEqual((result["restart_requested"], result["waited"]), (False, False))

    def test_nothing_else_is_written_and_the_card_is_not_touched(self):
        before = self.files(self.card)
        self.tool(PlayerHost(self)).install(self.image)
        self.assertEqual(self.files(self.root), [DEBUG + "/report0.txt", TIME_PATH, echo_debug.UPDATE_FILE])
        self.assertEqual(self.files(self.card), before)

    def test_a_file_that_is_not_this_players_image_is_refused_before_anything_is_written(self):
        host = PlayerHost(self)
        for data in (image(brand=b"Rockchip"), image(brand=b"EchoPod"), b"", image(size=0x800)):
            self.image.write_bytes(data)
            self.assert_refused("not_an_image", lambda: self.tool(host).install(self.image))
        self.assert_refused("image_missing", lambda: self.tool(host).install(self.root / "none.img"))
        self.assertFalse((self.root / echo_debug.UPDATE_FILE).exists())
        self.assertEqual(host.ejected, [])

    def test_without_a_player_nothing_is_written(self):
        self.assert_refused("player_not_connected",
                            lambda: self.tool(FakeHost(usb_tree())).install(self.image))
        self.assertFalse((self.root / echo_debug.UPDATE_FILE).exists())

    def test_a_wrong_volume_is_refused_before_anything_is_written(self):
        host = FakeHost(usb_tree(player([medium("Internal storage", "UNTITLED", self.root)])))
        self.assert_refused("wrong_volume_name", lambda: self.tool(host).install(self.image))
        host = FakeHost(usb_tree(player([medium("SD card", "ECHO", self.root)])))
        self.assert_refused("internal_drive_not_identified", lambda: self.tool(host).install(self.image))
        self.assertFalse((self.root / echo_debug.UPDATE_FILE).exists())

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
        for path in (self.root / "notes.txt", self.root / echo_debug.DATA_DIRECTORY / "settings.bin",
                     self.root / "Music" / echo_debug.UPDATE_FILE, self.debug / "deeper" / "run.txt",
                     self.card / echo_debug.UPDATE_FILE, self.card / echo_debug.DATA_DIRECTORY / "debug" / "run.txt",
                     self.debug / ".." / ".." / "escape.txt"):
            self.assert_refused("write_refused", lambda: tool.write(self.root, path, b"x"))
        self.assertEqual(self.files(self.root), [DEBUG + "/report0.txt"])
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
                            lambda: tool.write(other, other / echo_debug.UPDATE_FILE, b"x"))
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

    def test_probes_come_back_with_their_line_their_words_and_the_two_levels(self):
        self.script.write_text("build any\npeek 0x500100E8\ngpio 0.14 function 1\npoke 0x60080000 1\n",
                               encoding="utf-8")
        probes = [(2, 5120, "0x00000002", "0x00000002", 1, "unknown"),
                  (3, 5184, "0x00000011", "0x00000100", 0, 1)]
        script = self.tool(self.player_runs(probes=probes)).run(self.script)["script"]
        self.assertEqual(script["probes"][0],
                         {"line": 2, "ms": 5120, "before": 2, "after": 2, "cable": 1, "charge_pin": None,
                          "command": "peek 0x500100E8"})
        self.assertEqual(script["probes"][1]["command"], "gpio 0.14 function 1")
        self.assertEqual(script["probes"][1]["pin_before"], {"function": 0, "output": True, "level": 1})
        self.assertEqual(script["probes"][1]["pin_after"], {"function": 1, "output": False, "level": 0})
        self.assertEqual((script["probes"][1]["cable"], script["probes"][1]["charge_pin"]), (0, 1))

    def test_clock_reads_come_back_with_every_register_the_decoded_time_and_the_trust_flag(self):
        self.script.write_text("build any\nrtc read\nrtc read\nrtc read\n", encoding="utf-8")
        reads = ["script.rtc: line=2 ms=5120 status=ok registers=00000500120306012680808080000000 vl=0 "
                 "time=2026-01-03T12:00:05 weekday=6 century=0",
                 "script.rtc: line=3 ms=5200 status=ok registers=0800ffffffffffffffffffffffffffff vl=1 "
                 "time=invalid weekday=7 century=1",
                 "script.rtc: line=4 ms=5300 status=no_ack registers=unknown vl=unknown time=unknown "
                 "weekday=unknown century=unknown"]
        script = self.tool(self.player_runs(soaks=reads)).run(self.script)["script"]
        self.assertEqual(script["rtc"][0],
                         {"line": 2, "ms": 5120, "status": "ok",
                          "registers": [0, 0, 5, 0, 0x12, 3, 6, 1, 0x26, 0x80, 0x80, 0x80, 0x80, 0, 0, 0],
                          "vl": 0, "time": "2026-01-03T12:00:05", "weekday": 6, "century": 0})
        self.assertEqual((script["rtc"][1]["vl"], script["rtc"][1]["time"]), (1, "invalid"))
        self.assertEqual(script["rtc"][2],
                         {"line": 4, "ms": 5300, "status": "no_ack", "registers": None, "vl": None,
                          "time": None, "weekday": None, "century": None})

    def test_a_clock_read_with_damaged_registers_is_a_bad_report(self):
        with self.assertRaises(echo_debug.DebugError) as raised:
            echo_debug.parse_rtc("line=2 ms=1 status=ok registers=00zz vl=0")
        self.assertEqual(raised.exception.code, "bad_report")

    def test_a_soak_comes_back_with_its_slope_its_load_and_both_ends(self):
        self.script.write_text("build any\nsupply off\nscreen off\nsoak 30\n", encoding="utf-8")
        result = self.tool(self.player_runs(soaks=[SOAK_LINE])).run(self.script)
        soak = result["script"]["soaks"][0]
        self.assertEqual((soak["line"], soak["minutes"], soak["end"], soak["samples"]), (4, 30, "done", 20))
        self.assertEqual((soak["first_mv"], soak["last_mv"], soak["mv_per_hour"]), (4012, 3950, -142))
        self.assertEqual((soak["busy_percent"], soak["clock_mhz_start"], soak["clock_mhz_end"]), (12, 200, 200))
        self.assertEqual((soak["supply_off_start"], soak["supply_off_end"]), (1, 1))
        self.assertEqual((soak["audio_start"], soak["screen_start"], soak["cable_end"]), (1, 0, 1))
        self.assertEqual((soak["charge_pin_start"], soak["charge_pin_end"]), (1, None))

    def test_the_wait_for_a_script_grows_by_the_minutes_of_its_soaks(self):
        self.assertEqual(echo_debug.soak_seconds("build any\nsoak 30\nplay\n  SOAK 5 \n# soak 9\nsoak x\n"),
                         35 * 60)
        self.assertEqual(echo_debug.run_timeout("build any\nplay\n", None), echo_debug.DEFAULT_TIMEOUT)
        self.assertEqual(echo_debug.run_timeout("build any\nsoak 180\n", None),
                         echo_debug.DEFAULT_TIMEOUT + 180 * 60)
        self.assertEqual(echo_debug.run_timeout("build any\nsoak 180\n", 12000.0), 12000.0)
        self.script.write_text("build any\nsoak 30\n", encoding="utf-8")
        result = self.tool(self.player_runs()).run(self.script)
        self.assertEqual(result["timeout_s"], echo_debug.DEFAULT_TIMEOUT + 30 * 60)

    def test_a_soak_the_player_never_ends_is_waited_for_its_whole_length_and_no_longer(self):
        self.script.write_text("build any\nsoak 30\n", encoding="utf-8")
        tool = self.tool(PlayerHost(self, returns=False))
        self.assert_refused("player_did_not_return", lambda: tool.run(self.script))
        self.assertGreaterEqual(self.clock.now, echo_debug.DEFAULT_TIMEOUT + 30 * 60)
        self.assertLess(self.clock.now, echo_debug.DEFAULT_TIMEOUT + 30 * 60 + 120)

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
        (self.card / echo_debug.DATA_DIRECTORY / "debug" / "run.txt").write_text("old")
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
        self.assertEqual(self.files(self.root), [DEBUG + "/report0.txt", DEBUG + "/shot0.565"])


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
