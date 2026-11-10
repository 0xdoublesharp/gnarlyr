import argparse
import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
sys.path.insert(0, str(PROJECT.parent / "tools"))

import echo_debug
import lang_pack
import make_image
from identity import IDENTITY

SKIP_EXIT_CODE = 77
DEFAULT_IMAGE = PROJECT / "build" / "device" / IDENTITY.update_file
DEFAULT_ELF = PROJECT / "build" / "device" / "platform" / "device" / "echo_device.elf"
DEFAULT_USB_OVERLAY_ELF = DEFAULT_ELF.with_name("echo_usb_overlay.elf")
CONFIG = {"image": DEFAULT_IMAGE, "elf": DEFAULT_ELF, "usb_overlay_elf": DEFAULT_USB_OVERLAY_ELF}

FIRST_FRAME_LIMIT_MS = 1000
DEEP_TRACK = 12
FIRST_AUDIO_LIMIT_MS = 1500
TRACK_GAP_LIMIT_MS = 1000
FADE_LIMIT_MS = 1500
SCAN_RECORD_POLL_MS = 100
FAT_READS_PER_SECOND_LIMIT = 40
SPIN_POLLS_PER_REST = 10
WAIT_REST_TOLERANCE_MS = 1
DATA_READS_PER_SECOND_MINIMUM = 250
WIDTH = 480
HEIGHT = 222
FIRST_ROW = 49
LAST_ROW = FIRST_ROW + HEIGHT - 1
SLOT_SECTORS = 0x2800
CHUNK_SECTORS = 16
BACKLIGHT_HZ = 25000
MINI_STOCK_SIZE = 0x2000004
LOADER_FLAG_VALUE = 0xFFFF18AF
LOADER_FLAG_REGISTER = 0x4C
LOADER_SELECT_REGISTER = 0xC4
LOADER_SELECT_VALUE = 0x01000000
PWM_PERIOD = 0x04
PWM_CONTROL = 0x0C
PWM_RUN = 0x10003
GPIO_DATA = 0x00
MADCTL_CHOICES = (0x28, 0x68)
MADCTL_COMMAND = 0x36
MADCTL_SWAP = 0x20
MADCTL_MIRROR_ROWS = 0x80
MADCTL_MIRROR_COLUMNS = 0x40
PORTRAIT_COLUMNS = (FIRST_ROW, FIRST_ROW + HEIGHT - 1)
PORTRAIT_LAST_ROW = WIDTH - 1
SETTINGS_VERSION = 10
SETTINGS_ORIENTATION_FIELD = 26
SETTINGS_CHECKSUM_FIELD = 28
REPORT_BYTES = 16384
REPORT_SLOTS = 3
COLD_REPORT_MS = 10000
POWER_HOLD_MS = 2600
DEBUG_FOLDER = IDENTITY.data_directory + "/debug"
RUN_PATH = "/" + DEBUG_FOLDER + "/run.txt"
RESTART_PATH = "/" + DEBUG_FOLDER + "/restart.req"
RESTART_REQUEST = b"RESTART AFTER INSTALL\n"
RESTART_ANSWER = b"CONNECT AFTER RESTART\n"
RECONNECT_WAIT_MS = 6000
CHARGER_MS = 10000
SCRIPT_LIMIT_MS = 40000
RECONNECT_OFF_MS = 1000
QUIET_AFTER_MS = 4000
BOOT_FLAG_CLEAR = 0xFFFF0000
REPORT_START_LIMIT_MS = 250
CABLE_STEADY_MS = 1000
CLOCK_TICK_MS = 1
GRF_WORDS = ("e8", "84", "b4", "b8", "98")
GRF_WORDS_POWERED = [2, 3, 2, 0, 4]
CABLE_NOTICED_LIMIT_MS = 100
CLOCK_SLACK_MS = 5
CODE_WINDOW_BYTES = 0x2666C
PROFILE_BUCKET_BYTES = 128
KEY_NEXT = 100
KEY_PLAY = 250
KEY_BACK = 500
KEY_HOLD_MS = 150
KEY_SETTLE_MS = 350
ABOUT_PROFILER_ROW = 15
ABOUT_USB_SENSE_ROW = 16
ABOUT_CONNECT_USB_ROW = 17
USB_POWER_DOWN = [("grf", 0xB4, 0x00010001), ("grf", 0xB4, 0x000E0004), ("grf", 0xB8, 0x00010001),
                  ("grf", 0x98, 0x00040000), ("cru", 0xC0, 0x00020002), ("cru", 0xC0, 0x00040004),
                  ("cru", 0xC0, 0x00010001), ("cru", 0x80, 0x00800080), ("cru", 0x98, 0x04000400)]
SOFT_DISCONNECT = ("usb", "soft disconnect", 0)
USB_PHY_ON = [("grf", 0xB8, 0x00010000), ("grf", 0xB4, 0x000E0002), ("grf", 0x98, 0x00040004),
              ("grf", 0xB4, 0x00010000)]
USB_PHY_TUNING = [("grf", 0x88, 0x0C000C00), ("grf", 0x84, 0xE01F0003), ("grf", 0x8C, 0x001C0014),
                  ("grf", 0x80, 0x00070004)]
USB_POWER_UP = [("cru", 0x80, 0x00800000), ("cru", 0x98, 0x04000000), ("cru", 0xC0, 0x00020000),
                ("cru", 0xC0, 0x00040000), ("cru", 0xC0, 0x00010000)]
PROFILED_MS = 1500
LANGUAGE_RAM = 0x0309D000
MISSING_TEXT = "Text is missing: reinstall the update"
STEPS_AFTER_NOTICE = 20
FNV_START = 2166136261
FNV_PRIME = 16777619

GAMMA_POSITIVE = (0xF0, 0x0B, 0x12, 0x09, 0x0A, 0x26, 0x39, 0x54, 0x4E, 0x38, 0x13, 0x13, 0x2E,
                  0x34)
GAMMA_NEGATIVE = (0xF0, 0x10, 0x15, 0x0D, 0x0C, 0x07, 0x38, 0x43, 0x4D, 0x3A, 0x16, 0x15, 0x30,
                  0x35)


def expected_init(madctl):
    return [
        (0x11, ()), (0xF0, (0xC3,)), (0xF0, (0x96,)), (0x36, (madctl,)), (0x3A, (0x55,)),
        (0xB4, (0x01,)), (0xB6, (0x20, 0x02, 0x3B)), (0xB7, (0xC6,)), (0xB9, (0x02, 0xE0)),
        (0xC0, (0xC0, 0x64)), (0xC1, (0x1D,)), (0xC2, (0xA7,)), (0xC5, (0x28,)),
        (0xE8, (0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33)), (0xE0, GAMMA_POSITIVE),
        (0xE1, GAMMA_NEGATIVE), (0xF0, (0x3C,)), (0xF0, (0x69,)), (0x35, (0x00,)), (0x29, ()),
        (0x21, ()), (0x2A, (0x00, 0x31, 0x01, 0x0E)), (0x2B, (0x00, 0x00, 0x01, 0xDF)),
        (0x2C, ()),
    ]


def english(name):
    return dict(lang_pack.read_catalogue())[name]


def settings_file(orientation):
    data = bytearray(SETTINGS_CHECKSUM_FIELD + 4)
    data[0:4] = b"DSST"
    data[4] = SETTINGS_VERSION
    data[6:9] = bytes((7, 2, 0))
    data[13] = 13
    data[14] = 2
    data[16] = 35
    data[19:24] = bytes((10,) * 5)
    data[24] = 1
    data[SETTINGS_ORIENTATION_FIELD] = orientation
    checksum = FNV_START
    for byte in data[:SETTINGS_CHECKSUM_FIELD]:
        checksum = (checksum ^ byte) * FNV_PRIME & 0xFFFFFFFF
    data[SETTINGS_CHECKSUM_FIELD:] = checksum.to_bytes(4, "little")
    return bytes(data)


def pin_high_index(machine, port, pin):
    for index, entry in enumerate(machine.gpio_log):
        _, entry_port, _, _, data, direction, _ = entry
        if entry_port == port and data >> pin & 1 and direction >> pin & 1:
            return index
    return None


def pin_ever_output(machine, port, pin):
    return any(entry[1] == port and entry[5] >> pin & 1 for entry in machine.gpio_log)


def summary(name, session):
    machine = session.machine
    panel = machine.panel
    print("\n[%s] %d ms emulated, %d VOP commands, %d windows, %d pixels, %d sweeps, "
          "%d eMMC writes, %d unmodelled registers" % (
              name, machine.milliseconds(), len(panel.groups), len(panel.windows),
              panel.pixel_total, len(panel.sweeps), len(session.card.writes),
              len(machine.unknown_mmio)))


class EchoImageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from echo_emulator import session
        cls.session_module = session
        cls.image = Path(CONFIG["image"]).read_bytes()
        cls.elf = CONFIG["elf"]
        cls.usb_overlay_elf = CONFIG["usb_overlay_elf"]

    def session(self, files=(), image=None, vbus=False, volume=None, rest_in_waits=True):
        return self.session_module.Session(image or self.image, self.elf, files=files,
                                           vbus_after_first_frame=vbus, volume=volume,
                                           usb_overlay_elf=self.usb_overlay_elf,
                                           rest_in_waits=rest_in_waits)

    def assert_healthy(self, session):
        machine = session.machine
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.panics, [])

    def assert_only_the_user_volume_changed(self, session):
        layout = session.layout
        first = layout["volume_start"]
        last = first + layout["volume_sectors"]
        for _, lba, count in session.card.writes:
            self.assertTrue(first <= lba and lba + count <= last, (lba, count))
        for lba in session.changed_sectors():
            self.assertTrue(first <= lba < last, lba)

    def assert_windows_inside_panel(self, panel):
        drawn = [window for window in panel.windows if window.pixels > 0]
        self.assertTrue(drawn)
        for window in drawn:
            self.assertTrue(0 <= window.columns[0] <= window.columns[1] < WIDTH, window.columns)
            self.assertTrue(FIRST_ROW <= window.rows[0] <= window.rows[1] <= LAST_ROW,
                            window.rows)
            self.assertEqual(window.pixels, window.width * window.height)
        self.assertEqual(panel.out_of_range, 0)

    def test_normal_boot_initialises_the_panel_and_draws_the_first_frame(self):
        session = self.session()
        self.assertEqual(session.run_to_first_frame(), "condition met")
        panel = session.panel
        init = panel.init_commands()
        madctl = dict(init)[0x36][0]
        self.assertIn(madctl, MADCTL_CHOICES)
        self.assertEqual(init, expected_init(madctl))
        self.assertEqual(len(panel.sweeps), 2)
        clear, first = panel.sweeps
        self.assertEqual((clear.windows, clear.nonzero), (HEIGHT, 0))
        self.assertEqual(first.windows, 10)
        self.assertEqual(first.nonzero, WIDTH * HEIGHT)
        self.assertEqual(panel.painted_pixels(), WIDTH * HEIGHT)
        self.assert_windows_inside_panel(panel)
        self.assert_healthy(session)
        summary("first frame", session)

    def portrait_windows(self, panel):
        return [window for window in panel.windows if window.columns == PORTRAIT_COLUMNS and window.pixels > 0]

    def boot_saved_orientation(self, orientation):
        from echo_emulator.fat_volume import Fat32Volume
        volume = Fat32Volume()
        volume.add_directory(".doublesharp")
        volume.add_file("settings.bin", settings_file(orientation), directory=".doublesharp")
        session = self.session(volume=volume)
        panel = session.panel
        reason = session.machine.run(
            until=lambda: sum(w.pixels for w in self.portrait_windows(panel)) >= WIDTH * HEIGHT,
            until_ms=self.session_module.FRAME_LIMIT_MS)
        self.assertEqual(reason, "condition met")
        return session

    def assert_turned_before_the_first_portrait_frame(self, session, madctl_change):
        panel = session.panel
        init = panel.init_commands()
        landscape = dict(init)[MADCTL_COMMAND][0]
        self.assertEqual(init, expected_init(landscape))
        turns = [params for command, params in panel.groups if command == MADCTL_COMMAND][1:]
        self.assertEqual(turns, [[madctl_change(landscape & ~MADCTL_SWAP)]])
        first = panel.windows.index(self.portrait_windows(panel)[0])
        for window in panel.windows[first:]:
            self.assertEqual(window.columns, PORTRAIT_COLUMNS)
            self.assertTrue(0 <= window.rows[0] <= window.rows[1] <= PORTRAIT_LAST_ROW)
        self.assertEqual(max(window.rows[1] for window in panel.windows[first:]),
                         PORTRAIT_LAST_ROW)
        self.assert_healthy(session)

    def test_a_saved_portrait_turns_the_panel_before_the_first_frame(self):
        session = self.boot_saved_orientation(1)
        self.assert_turned_before_the_first_portrait_frame(
            session, lambda upright: upright ^ MADCTL_MIRROR_ROWS)
        summary("portrait", session)

    def test_a_saved_flipped_portrait_mirrors_the_other_axis(self):
        session = self.boot_saved_orientation(2)
        self.assert_turned_before_the_first_portrait_frame(
            session, lambda upright: upright ^ MADCTL_MIRROR_COLUMNS)

    def test_normal_boot_reaches_the_home_screen_using_only_the_user_volume(self):
        session = self.session()
        self.assertEqual(session.run_to_home_screen(), "condition met")
        machine = session.machine
        panel = session.panel
        self.assertEqual(len(panel.lit_sweeps()), 2)
        self.assertGreater(panel.colors(), 8)
        self.assert_windows_inside_panel(panel)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.assertFalse(machine.vbus)
        self.assertTrue(machine.watchdog_running)
        self.assertGreater(machine.watchdog_kicks, 0)
        self.assertGreater(len(session.card.reads), 0)
        summary("home screen", session)

    def test_power_hold_comes_first_and_on_battery_the_usb_supply_pins_stay_low(self):
        session = self.session()
        session.run_to_home_screen()
        machine = session.machine
        self.assertIsNotNone(pin_high_index(machine, 2, 6))
        self.assertIsNone(pin_high_index(machine, 0, 14))
        self.assertIsNone(pin_high_index(machine, 2, 0))
        self.assertTrue(pin_ever_output(machine, 0, 14))
        self.assertTrue(pin_ever_output(machine, 2, 0))
        self.assertEqual(machine.gpio[2].regs[GPIO_DATA] >> 6 & 1, 1)
        self.assertEqual(machine.gpio[2].regs[4] >> 6 & 1, 1)
        self.assertEqual(self.usb_supply_pins(machine), (0, 0))
        self.assertFalse(pin_ever_output(machine, 2, 1))

    def test_started_by_the_cable_the_usb_supply_pins_go_high_after_the_power_hold(self):
        session = self.session_module.Session(self.image, self.elf, vbus_at_start=True,
                                              usb_overlay_elf=self.usb_overlay_elf)
        machine = session.machine
        self.assertEqual(session.run_to_first_frame(), "condition met")
        hold = pin_high_index(machine, 2, 6)
        charge = pin_high_index(machine, 0, 14)
        supply = pin_high_index(machine, 2, 0)
        self.assertIsNotNone(hold)
        self.assertIsNotNone(charge)
        self.assertIsNotNone(supply)
        self.assertLess(hold, charge)
        self.assertLess(hold, supply)
        self.assertEqual(self.usb_supply_pins(machine), (1, 1))

    def test_the_usb_supply_pins_follow_the_cable_and_a_waking_screen_leaves_them_alone(self):
        session = self.session()
        machine = session.machine
        session.run_to_home_screen()
        self.assertEqual(self.usb_supply_pins(machine), (0, 0))
        machine.vbus = True
        machine.run(until_ms=machine.milliseconds() + 200)
        self.assertEqual(self.usb_supply_pins(machine), (1, 1))
        machine.vbus = False
        machine.run(until_ms=machine.milliseconds() + 200)
        self.assertEqual(self.usb_supply_pins(machine), (0, 0))
        self.assertEqual(machine.usb_log, [])
        self.assert_healthy(session)

    @staticmethod
    def usb_supply_pins(machine):
        return (machine.gpio[0].regs[GPIO_DATA] >> 14 & 1, machine.gpio[2].regs[GPIO_DATA] & 1)

    def test_before_the_main_loop_core_1_is_parked_and_unused_clocks_are_stopped(self):
        session = self.session()
        machine = session.machine
        session.run_to_home_screen()
        parked = [("cru", 0xCC, 0x00040004), ("cru", 0xC8, 0x00070007), ("cru", 0xC4, 0x40004000),
                  ("cru", 0x8C, 0x03FF03FF), ("cru", 0x90, 0x1F0F1F0F), ("cru", 0x80, 0x00040004),
                  ("cru", 0xA4, 0x00040004), ("cru", 0x88, 0x00070007), ("cru", 0x84, 0x50505050),
                  ("cru", 0xA0, 0x7D327D32), ("cru", 0xA4, 0x00020002)]
        written = [entry for entry in machine.register_log if entry in parked]
        self.assertEqual(written[:len(parked)], parked)
        self.assert_healthy(session)

    def test_a_resting_screen_runs_core_0_at_half_speed_and_a_key_brings_full_speed_back(self):
        session = self.session()
        machine = session.machine
        session.run_to_home_screen()
        machine.run(until=lambda: machine.coast_log and machine.coast_log[-1][1] == 2,
                    until_ms=machine.milliseconds() + 5000)
        self.assertEqual(machine.coast_log[-1][1], 2)
        self.assertEqual(machine.core_hz, 100_000_000)
        coasting_since = machine.milliseconds()
        machine.run(until_ms=coasting_since + 1500)
        self.assertEqual(machine.milliseconds() - coasting_since, 1500)
        frames = len(machine.panel.groups)
        machine.press_key(KEY_NEXT, delay_ms=20, hold_ms=KEY_HOLD_MS)
        machine.run(until=lambda: len(machine.panel.groups) > frames,
                    until_ms=machine.milliseconds() + 1000)
        self.assertGreater(len(machine.panel.groups), frames)
        self.assertEqual(machine.coast_log[-1][1], 1)
        self.assertEqual(machine.core_hz, 200_000_000)
        self.assert_healthy(session)

    def test_backlight_runs_at_25_khz_and_only_after_the_first_frame(self):
        session = self.session()
        session.run_to_home_screen()
        machine = session.machine
        periods = [(value, pclk) for _, offset, value, pclk, _ in machine.pwm_log
                   if offset == PWM_PERIOD]
        self.assertTrue(periods)
        for value, pclk in periods:
            self.assertEqual(pclk // value, BACKLIGHT_HZ)
        self.assertIn(PWM_RUN, [value for _, offset, value, _, _ in machine.pwm_log
                                if offset == PWM_CONTROL])
        first_frame_marker = session.panel.lit_sweeps()[0].groups
        self.assertGreaterEqual(machine.pwm_log[0][4], first_frame_marker)
        backlight = pin_high_index(machine, 2, 4)
        self.assertGreaterEqual(machine.gpio_log[backlight][6], first_frame_marker)

    def test_loader_request_is_consumed_and_the_loader_reboot_is_reached(self):
        session = self.session(files=[("MASKROM.REQ", b"MASKROM REQ\n", None)])
        self.assertEqual(session.run_until_reset(), "system reset requested")
        machine = session.machine
        self.assert_healthy(session)
        self.assertEqual(session.read_file("MASKROM.REQ", 12), bytes(11) + b"\n")
        layout = session.layout
        volume = range(layout["volume_start"], layout["volume_start"] + layout["volume_sectors"])
        self.assertEqual([(lba, count) for _, lba, count in session.card.writes],
                         [(session.file_lba("MASKROM.REQ"), 1)])
        self.assertIn(session.file_lba("MASKROM.REQ"), volume)
        self.assertEqual(session.changed_sectors(), [session.file_lba("MASKROM.REQ")])
        flag = [(stamp, value) for stamp, value in machine.pmu_flag_writes
                if value == LOADER_FLAG_VALUE]
        self.assertEqual(len(flag), 1)
        self.assertEqual(machine.pmu.regs[LOADER_FLAG_REGISTER], LOADER_FLAG_VALUE & 0xFFFF)
        self.assertGreater(flag[0][0], session.card.writes[0][0])
        select = [value for _, offset, value in machine.grf_log if offset == LOADER_SELECT_REGISTER]
        self.assertEqual(select[-1], LOADER_SELECT_VALUE)
        self.assertEqual(machine.systick.vtor, 0)
        self.assertEqual(session.panel.lit_sweeps(), [])
        summary("loader request", session)

    def test_a_loader_request_with_other_text_is_left_alone(self):
        session = self.session(files=[("MASKROM.REQ", b"MASKROM RES\n", None)])
        self.assertEqual(session.run_to_first_frame(), "condition met")
        self.assertIsNone(session.machine.reset_requested)
        self.assertEqual(session.machine.pmu_flag_writes, [])
        self.assertEqual(session.read_file("MASKROM.REQ", 12), b"MASKROM RES\n")
        self.assert_only_the_user_volume_changed(session)

    def built_library_tracks(self, session):
        for data in self.volume_sectors(session):
            if data[:4] == b"DSDB":
                return struct.unpack_from("<I", data, 12)[0]
        return 0

    def test_without_an_sd_card_music_on_the_internal_volume_is_scanned(self):
        track = (PROJECT / "tests" / "fixtures" / "mp3" /
                 "cbr_128_44k_stereo_tagged.mp3").read_bytes()
        session = self.session(files=[("TRACK01.MP3", track, None)])
        session.machine.stop_at("hal_system_return_scratch")
        reason = session.machine.run(until_ms=self.session_module.FRAME_LIMIT_MS)
        self.assertEqual(reason, "reached hal_system_return_scratch")
        self.assertEqual(self.built_library_tracks(session), 1)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.assertEqual(session.read_file("TRACK01.MP3", len(track)), track)

    def volume_sectors(self, session):
        start = session.layout["volume_start"]
        return [data for lba, data in session.disk.sectors.items() if lba >= start]

    def recorded_version(self, session):
        for data in self.volume_sectors(session):
            if data[:4] == b"DSSC":
                return bytes(data[16:32]).rstrip(b"\0").decode("ascii")
        return None

    def test_the_scan_record_names_the_version_and_the_build_tag(self):
        track = (PROJECT / "tests" / "fixtures" / "mp3" /
                 "cbr_128_44k_stereo_tagged.mp3").read_bytes()
        session = self.session(files=[("TRACK01.MP3", track, None)])
        machine = session.machine
        deadline = self.session_module.FRAME_LIMIT_MS
        while self.recorded_version(session) is None and machine.milliseconds() < deadline:
            machine.run(until_ms=machine.milliseconds() + SCAN_RECORD_POLL_MS)
        build_tag = self.image[0x30:0x38].rstrip(b"\0").decode("ascii")
        self.assertEqual(self.recorded_version(session), "1.0.0-" + build_tag)
        self.assert_healthy(session)

    def assert_every_write_read_back(self, accesses):
        for index, access in enumerate(accesses):
            if access[0] != "W":
                continue
            lba, count = access[1], access[2]
            covered = []
            for later in accesses[index + 1:]:
                if later[0] != "R" or len(covered) >= count:
                    break
                covered.extend(range(later[1], later[1] + later[2]))
            self.assertEqual(covered[:count], list(range(lba, lba + count)))

    def newer_image(self):
        return self.session_module.with_date(self.image, (2099, 1, 1))

    def test_a_newer_image_is_installed_slot_2_first_and_header_last(self):
        newer = self.newer_image()
        session = self.session(files=[("DBLSHARP.IMG", newer, None)], vbus=True)
        session.machine.note("updater_install")
        self.assertEqual(session.run_to_first_frame(), "condition met")
        session.machine.stop_at("board_wait_for_power_hold")
        self.assertEqual(session.run_until_waiting_for_key(presses=1),
                         "reached board_wait_for_power_hold")
        machine = session.machine
        self.assert_healthy(session)
        self.assertEqual(machine.reached["updater_install"], 1)
        self.assertGreaterEqual(machine.usb_connect_count, 1)
        slots = {name: session.layout[name] for name in ("slot_1", "slot_2")}
        inside = {name: range(start, start + SLOT_SECTORS) for name, start in slots.items()}
        volume_start = session.layout["volume_start"]
        writes = [item for item in session.card.writes if item[1] < volume_start]
        for _, lba, count in writes:
            self.assertEqual(count, CHUNK_SECTORS)
            self.assertTrue(any(lba in span and lba + count <= span.stop
                                for span in inside.values()), hex(lba))
        for lba in session.changed_sectors():
            self.assertTrue(lba >= volume_start or any(lba in span for span in inside.values()),
                            hex(lba))
        by_slot = {name: [item for item in writes if item[1] in span]
                   for name, span in inside.items()}
        self.assertEqual(len(writes), len(by_slot["slot_1"]) + len(by_slot["slot_2"]))
        self.assertLess(max(stamp for stamp, _, _ in by_slot["slot_2"]),
                        min(stamp for stamp, _, _ in by_slot["slot_1"]))
        header_word = struct.unpack_from("<I", newer)[0]
        writes_in_order = [(lba, count, word) for kind, lba, count, *rest in session.card.accesses
                           if kind == "W" and lba < volume_start for word in rest]
        for name, start in slots.items():
            sequence = [item for item in writes_in_order if item[0] in inside[name]]
            self.assertEqual(sequence[0], (start, CHUNK_SECTORS, 0xFFFFFFFF))
            self.assertEqual(sequence[-1], (start, CHUNK_SECTORS, header_word))
            self.assertNotIn(start, [lba for lba, _, _ in sequence[1:-1]])
        self.assert_every_write_read_back(
            [item for item in session.card.accesses if item[0] != "W" or item[1] < volume_start])
        self.assertEqual(session.slot_image("slot_1"), newer[:self.session_module.SLOT_BYTES])
        self.assertEqual(session.slot_image("slot_2"), newer[:self.session_module.SLOT_BYTES])
        summary("update", session)
        installed = self.session(image=session.slot_image("slot_1") + newer[-4:])
        self.assertEqual(installed.run_to_first_frame(), "condition met")
        self.assertEqual(len(installed.panel.lit_sweeps()), 1)
        self.assert_healthy(installed)

    def test_the_running_image_offered_again_is_not_written(self):
        session = self.session(files=[("DBLSHARP.IMG", self.image, None)], vbus=True)
        session.machine.note("updater_install")
        session.machine.note("updater_check")
        session.run_to_first_frame()
        self.assertEqual(session.run_through_update_check(),
                         "reached usb_session_storage_changed")
        self.assertEqual(session.machine.reached["updater_check"], 1)
        self.assertNotIn("updater_install", session.machine.reached)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_an_echo_pod_image_is_refused_and_nothing_is_written(self):
        mini = self.session_module.with_brand(self.image, "EchoPod")
        self.assert_update_refused(("ECHOV497.IMG", mini, None))

    def test_a_podium_image_under_our_file_name_is_refused_and_nothing_is_written(self):
        podium = self.session_module.with_brand(self.image, "PODIUM")
        self.assert_update_refused(("DBLSHARP.IMG", podium, None))

    def test_a_mini_stock_size_image_is_refused_and_nothing_is_written(self):
        self.assert_update_refused(("ECHOV496.IMG", None, self.session_module.STOCK_SIZE))

    def assert_update_refused(self, update):
        session = self.session(files=[update], vbus=True)
        session.machine.note("updater_install")
        session.machine.note("updater_check")
        session.run_to_first_frame()
        self.assertEqual(session.run_until_waiting_for_key(presses=1),
                         "reached board_wait_for_key")
        self.assertEqual(session.machine.reached["updater_check"], 1)
        self.assertNotIn("updater_install", session.machine.reached)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        summary("refused " + update[0], session)

    def refuse_an_update(self, rest_in_waits):
        mini = self.session_module.with_brand(self.image, "EchoPod")
        session = self.session(files=[("ECHOV497.IMG", mini, None)], vbus=True,
                               rest_in_waits=rest_in_waits)
        session.machine.note("updater_check")
        polls = session.machine.trace("board_input_take_key_press")
        session.run_to_first_frame()
        self.assertEqual(session.run_until_waiting_for_key(presses=1),
                         "reached board_wait_for_key")
        self.assert_healthy(session)
        return session, polls

    def test_a_key_wait_that_spins_ends_like_one_that_rests(self):
        rested, rested_polls = self.refuse_an_update(True)
        spun, spun_polls = self.refuse_an_update(False)
        self.assertGreater(len(rested_polls), 0)
        self.assertGreater(len(spun_polls), SPIN_POLLS_PER_REST * len(rested_polls))
        self.assertEqual(rested.card.accesses, spun.card.accesses)
        self.assertEqual(rested.machine.reached, spun.machine.reached)
        self.assertEqual(rested.changed_sectors(), spun.changed_sectors())
        self.assertEqual(rested.machine.gpio_log[-1][1:6], spun.machine.gpio_log[-1][1:6])
        self.assertLessEqual(abs(rested.machine.milliseconds() - spun.machine.milliseconds()),
                             WAIT_REST_TOLERANCE_MS)
        summary("spinning key wait", spun)

    def boot_until_waiting_for_key(self, session):
        session.machine.note("updater_install")
        session.machine.note("updater_check")
        session.run_to_first_frame()
        session.machine.answer_waits_for_key(2)
        session.machine.stop_at("usb_session_storage_changed")
        session.machine.stop_at("board_wait_for_power_hold")
        return session.machine.run(until_ms=self.session_module.FRAME_LIMIT_MS)

    def test_an_update_file_is_installed_once_and_is_gone_afterwards(self):
        newer = self.newer_image()
        first = self.session(files=[("DBLSHARP.IMG", newer, None)], vbus=True)
        cluster = first.volume.first_cluster("DBLSHARP.IMG")
        self.assertEqual(self.boot_until_waiting_for_key(first),
                         "reached board_wait_for_power_hold")
        self.assertEqual(first.machine.reached["updater_install"], 1)
        self.assertFalse(first.file_exists("DBLSHARP.IMG"))
        self.assertEqual(first.fat_entry(cluster), 0)
        self.assertEqual(first.slot_image("slot_1"), newer[:self.session_module.SLOT_BYTES])
        second = first.reboot(image=first.slot_image("slot_1") + newer[-4:],
                              vbus_after_first_frame=True)
        self.assertEqual(self.boot_until_waiting_for_key(second),
                         "reached usb_session_storage_changed")
        self.assertNotIn("updater_install", second.machine.reached)
        self.assertNotIn("updater_check", second.machine.reached)
        self.assertEqual([w for w in second.card.writes if w[1] < second.layout["volume_start"]], [])
        self.assert_healthy(second)

    def test_restoring_an_older_build_does_not_reinstall_the_removed_update(self):
        newer = self.newer_image()
        first = self.session(files=[("DBLSHARP.IMG", newer, None)], vbus=True)
        self.boot_until_waiting_for_key(first)
        self.assertEqual(first.machine.reached["updater_install"], 1)
        first.write_slots(self.image)
        again = first.reboot(image=self.image, vbus_after_first_frame=True)
        self.boot_until_waiting_for_key(again)
        self.assertNotIn("updater_install", again.machine.reached)
        self.assertEqual(again.slot_image("slot_1"), self.image[:self.session_module.SLOT_BYTES])
        self.assert_healthy(again)

    def test_an_update_file_that_is_refused_or_already_installed_is_removed(self):
        mini = self.session_module.with_brand(self.image, "EchoPod")
        for name, data in (("ECHOV497.IMG", mini), ("DBLSHARP.IMG", self.image)):
            session = self.session(files=[(name, data, None)], vbus=True)
            self.boot_until_waiting_for_key(session)
            self.assertNotIn("updater_install", session.machine.reached)
            self.assertFalse(session.file_exists(name), name)
            self.assert_only_the_user_volume_changed(session)

    def test_the_usb_session_and_the_update_check_run_from_the_usb_overlay(self):
        session = self.session(files=[("DBLSHARP.IMG", self.image, None)], vbus=True)
        machine = session.machine
        machine.note("updater_check")
        machine.note("usb_storage_session")
        session.run_to_first_frame()
        self.assertFalse(machine.overlays[0].loaded(machine))
        self.assertEqual(session.run_through_update_check(),
                         "reached usb_session_storage_changed")
        self.assertTrue(machine.overlays[0].loaded(machine))
        window = range(make_image.OVERLAY_BASE, make_image.OVERLAY_BASE + make_image.OVERLAY_CAPACITY)
        self.assertIn(machine.current_pc(), window)
        self.assertEqual(machine.reached["usb_storage_session"], 1)
        self.assertEqual(machine.reached["updater_check"], 1)
        self.assertTrue(session.usb_overlay_reads("slot_1"))
        self.assertEqual(session.usb_overlay_reads("slot_2"), [])
        self.assert_healthy(session)

    def reports(self, session):
        """(path, parsed report) of every report file on the drive, newest first."""
        reader = session.fat()
        found = []
        for slot in range(REPORT_SLOTS):
            path = "/%s/report%d.txt" % (DEBUG_FOLDER, slot)
            data = reader.read(path)
            if data is not None:
                self.assertEqual(len(data), REPORT_BYTES)
                found.append((path, echo_debug.parse_report(data.decode("ascii"))))
        return sorted(found, key=lambda item: -item[1]["values"]["report.sequence"])

    def report_path(self, session):
        return self.reports(session)[0][0]

    def read_report(self, session):
        return self.reports(session)[0][1]

    def mark(self, machine, marks, name, after=None):
        def reached():
            if after is None or after in marks:
                marks.setdefault(name, (machine.stamp(), machine.microseconds()))

        machine.hook_symbol(name, reached)

    def test_a_usb_session_starts_by_writing_a_debug_report(self):
        session = self.session(vbus=True)
        machine = session.machine
        marks = {}
        for name in ("usb_storage_session", "debug_report_write", "usb_silence_audio"):
            self.mark(machine, marks, name)
        self.mark(machine, marks, "sd_start", after="debug_report_write")
        self.assertEqual(self.boot_until_waiting_for_key(session),
                         "reached usb_session_storage_changed")
        started, served = marks["usb_storage_session"], marks["usb_silence_audio"]
        report_ms = (marks["sd_start"][1] - marks["debug_report_write"][1]) / 1000
        report = self.read_report(session)
        values = report["values"]
        self.assertFalse(report["truncated"])
        self.assertEqual(report["unread_lines"], [])
        self.assertEqual((values["report.sequence"], values["report.reason"],
                          values["report.cold_start"]), (1, "usb_session", 1))
        self.assertEqual(values["firmware.name"], IDENTITY.name)
        self.assertEqual(values["firmware.version"], IDENTITY.version)
        self.assertEqual(values["firmware.build"],
                         self.image[0x30:0x38].rstrip(b"\0").decode("ascii"))
        self.assertTrue(started[1] // 1000 - CLOCK_SLACK_MS <= values["uptime_ms"]
                        <= served[1] // 1000 + CLOCK_SLACK_MS)
        self.assertIn(values["clock_mhz"], (96, 200))
        self.assertTrue(values["core0.idle_us"] <= values["core0.clock_us"])
        self.assertTrue(3900 <= values["battery.millivolts"] <= 4100)
        self.assertEqual(values["battery.usb_power"], 1)
        self.assertEqual((values["power.vbus"], values["power.usb_powered"],
                          values["power.charge_pin"]), (1, 1, 1))
        self.assertEqual([values["usb.grf_%s_start" % word] for word in GRF_WORDS], [0, 0, 0, 0, 0])
        self.assertEqual([values["usb.grf_%s" % word] for word in GRF_WORDS], GRF_WORDS_POWERED)
        self.assertEqual((values["usb.gotgctl"], values["usb.b_session"]), (0x00090000, 1))
        self.assertEqual((values["power.adc_raw"], values["power.adc_millivolts"]), (819, 3999))
        self.assertEqual(values["power.gauge_millivolts"], 3999)
        self.assertEqual(values["power.percent_shown"], values["battery.percent"])
        self.assertEqual((values["power.capped"], values["power.held"]), (0, 0))
        self.assertEqual((values["start.count"], values["start.warm"]), (1, 0))
        self.assertIsNone(values["start.previous_uptime_ms"])
        self.assertIsNone(values["fault.pc"])
        self.assertEqual(values["usb.last_end"], "none")
        self.assertIsNone(values["usb.internal_sectors_written"])
        self.assertEqual((values["audio.state"], values["audio.running"]), ("stopped", 0))
        self.assertIsNone(values["audio.underruns"])
        self.assertEqual(values["frame.count"] == 0, values["frame.total_avg_us"] is None)
        kinds = [event["kind"] for event in report["events"]]
        self.assertEqual(kinds[0], "screen")
        self.assertEqual(kinds[-1], "usb_start")
        self.assertEqual(values["events.recorded"], len(kinds))
        self.assertEqual(values["profile.state"], "off")
        self.assertEqual(values["profile.samples"], 0)
        self.assertEqual(values["profile.code_base"], make_image.CODE_BASE)
        self.assertEqual(values["profile.bucket_bytes"], PROFILE_BUCKET_BYTES)
        self.assertEqual(values["profile.buckets"], -(-CODE_WINDOW_BYTES // PROFILE_BUCKET_BYTES))
        self.assertEqual(report["profile_buckets"], {})
        sectors = session.file_sectors(self.report_path(session))
        stamps = [stamp for stamp, lba, count in session.card.writes if lba in sectors]
        self.assertTrue(stamps)
        self.assertTrue(all(started[0] < stamp < served[0] for stamp in stamps))
        delay_ms = (served[1] - started[1]) / 1000
        self.assertLess(delay_ms, REPORT_START_LIMIT_MS)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        text_bytes = len(session.fat().read(self.report_path(session)).rstrip(b"\n"))
        print("\n[debug report] %d bytes of text, written in %.1f ms; %.1f ms from the session's "
              "start to serving the host" % (text_bytes, report_ms, delay_ms))

    def test_a_second_session_reports_the_first_and_reuses_the_report_file(self):
        session = self.session(vbus=True)
        machine = session.machine
        machine.note("usb_session_storage_changed")
        machine.answer_waits_for_key(4)
        session.run_to_first_frame()
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        first_sectors = session.file_sectors(self.report_path(session))
        machine.run(until_ms=machine.milliseconds() + 1500)
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        values = report["values"]
        self.assertGreaterEqual(values["frame.count"], 1)
        self.assertGreater(values["frame.total_avg_us"], 0)
        self.assertEqual(values["usb.last_end"], "unplugged")
        self.assertEqual(values["usb.left_mounted"], 1)
        self.assertEqual(values["usb.internal_sectors_written"], 0)
        kinds = [event["kind"] for event in report["events"]]
        self.assertEqual(kinds.count("usb_start"), 2)
        self.assertEqual(kinds.count("usb_end"), 1)
        powers = [event["detail"] for event in report["events"] if event["kind"] == "power"]
        self.assertEqual(powers, ["1 1", "0 1", "1 1"])
        self.assertEqual(values["power.samples"], 1)
        sample = report["power_samples"][0]
        self.assertEqual((sample["vbus"], sample["charge_pin"], sample["playing"],
                          sample["usb_powered"]), (True, True, False, True))
        self.assertEqual((sample["minute"], sample["millivolts"]), (0, 3999))
        self.assertEqual(sample["percent"], values["battery.percent"])
        self.assertEqual((values["report.sequence"], values["report.cold_start"]), (2, 1))
        self.assertEqual(session.file_sectors(self.report_path(session)), first_sectors)
        self.assertEqual(session.fat().list("/" + IDENTITY.data_directory + "/debug"),
                         ["report0.txt"])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def press(self, machine, key):
        start = machine.milliseconds()
        machine.press_key(key, delay_ms=20, hold_ms=KEY_HOLD_MS)
        machine.run(until_ms=start + 20 + KEY_HOLD_MS + KEY_SETTLE_MS)

    def switch_profiler_on(self, machine):
        for key in [KEY_NEXT, KEY_PLAY, KEY_PLAY] + [KEY_NEXT] * ABOUT_PROFILER_ROW + [KEY_PLAY]:
            self.press(machine, key)

    def index_of(self, log, sequence, start=0):
        for index in range(start, len(log) - len(sequence) + 1):
            if log[index:index + len(sequence)] == sequence:
                return index
        return None

    def test_a_cable_must_hold_for_a_second_before_usb_is_powered_up(self):
        session = self.session()
        machine = session.machine
        listens = machine.trace("usb_session_listen")
        session.run_to_home_screen()
        machine.run(until_ms=machine.milliseconds() + 300)
        machine.vbus = True
        machine.run(until_ms=machine.milliseconds() + 700)
        machine.vbus = False
        machine.run(until_ms=machine.milliseconds() + 100)
        self.assertEqual(listens, [])
        self.assertEqual(machine.usb_log, [])
        machine.vbus = True
        plugged_ms = machine.milliseconds()
        machine.run(until=lambda: listens, until_ms=plugged_ms + 3000)
        self.assertEqual(len(listens), 1)
        self.assertGreaterEqual(listens[0], plugged_ms + CABLE_STEADY_MS - CLOCK_TICK_MS)
        self.assertLess(listens[0], plugged_ms + CABLE_STEADY_MS + CABLE_NOTICED_LIMIT_MS)
        self.assert_healthy(session)

    def test_on_a_charger_usb_keeps_listening_until_a_host_appears_or_the_cable_leaves(self):
        session = self.session()
        machine = session.machine
        for name in ("usb_session_storage_changed", "usb_power_down"):
            machine.note(name)
        machine.answer_waits_for_key(4)
        machine.host_absent = True
        session.run_to_home_screen()
        machine.vbus = True
        machine.run(until_ms=machine.milliseconds() + CHARGER_MS)
        self.assertEqual([event for _, event in machine.usb_log], ["soft disconnect", "pull-up on"])
        self.assertNotIn("usb_power_down", machine.reached)
        machine.vbus = False
        machine.run(until=lambda: "usb_power_down" in machine.reached,
                    until_ms=machine.milliseconds() + 1000)
        self.assertEqual(machine.reached["usb_power_down"], 1)
        self.assertEqual(machine.usb_log[-1][1], "soft disconnect")
        machine.vbus = True
        machine.run(until_ms=machine.milliseconds() + CHARGER_MS)
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertEqual(machine.reached["usb_power_down"], 1)
        self.assertNotIn("usb_session_storage_changed", machine.reached)
        machine.host_arrives()
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(machine.usb_connect_count, 2)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.print_usb_log("charger, then a host", machine)

    def test_usb_power_up_writes_the_words_stock_writes(self):
        session = self.session(vbus=True)
        machine = session.machine
        self.assertEqual(self.boot_until_waiting_for_key(session),
                         "reached usb_session_storage_changed")
        log = [entry for entry in machine.register_log if entry[0] in ("grf", "cru")]
        up = self.index_of(log, USB_POWER_UP + USB_PHY_ON + USB_PHY_TUNING)
        self.assertIsNotNone(up)
        self.assertIsNotNone(self.index_of(log, USB_POWER_DOWN, up))
        self.assert_healthy(session)

    def writes_after_last_disconnect(self, machine, since=0):
        log = machine.register_log[since:]
        last = len(log) - 1 - log[::-1].index(SOFT_DISCONNECT)
        return log[last + 1:]

    def test_usb_sense_kept_on_soft_disconnects_but_leaves_the_usb_hardware_powered(self):
        session = self.session()
        machine = session.machine
        machine.note("hal_system_return_scratch")
        machine.note("usb_session_storage_changed")
        machine.answer_waits_for_key(8)
        machine.run(until=lambda: "hal_system_return_scratch" in machine.reached,
                    until_ms=self.session_module.FRAME_LIMIT_MS)
        machine.run(until_ms=machine.milliseconds() + 1500)
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        machine.run(until_ms=machine.milliseconds() + 1500)
        self.assertEqual(self.writes_after_last_disconnect(machine)[:len(USB_POWER_DOWN)],
                         USB_POWER_DOWN)
        for key in [KEY_NEXT, KEY_PLAY, KEY_PLAY] + [KEY_NEXT] * ABOUT_USB_SENSE_ROW + [KEY_PLAY]:
            self.press(machine, key)
        kept_from = len(machine.register_log)
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        machine.run(until_ms=machine.milliseconds() + 1500)
        values = self.read_report(session)["values"]
        self.assertEqual((values["usb.sense_mode"], values["power.usb_powered"]), ("kept_on", 1))
        after = self.writes_after_last_disconnect(machine, kept_from)
        self.assertEqual([write for write in after if write in USB_POWER_DOWN], [])
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 3), "condition met")
        machine.run(until_ms=machine.milliseconds() + 1500)
        normal_from = len(machine.register_log)
        self.press(machine, KEY_PLAY)
        self.assertEqual(machine.register_log[normal_from:][-len(USB_POWER_DOWN):], USB_POWER_DOWN)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_the_profiler_row_fills_a_histogram_the_host_tool_symbolises(self):
        session = self.session()
        machine = session.machine
        machine.note("hal_system_return_scratch")
        machine.note("usb_session_storage_changed")
        machine.answer_waits_for_key(4)
        machine.run(until=lambda: "hal_system_return_scratch" in machine.reached,
                    until_ms=self.session_module.FRAME_LIMIT_MS)
        machine.run(until_ms=machine.milliseconds() + 1500)
        self.switch_profiler_on(machine)
        switched_ms = machine.milliseconds()
        machine.run(until_ms=switched_ms + PROFILED_MS)
        marks = {}
        self.mark(machine, marks, "usb_silence_audio")
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        report = self.read_report(session)
        values = report["values"]
        self.assertEqual(values["profile.state"], "on")
        served_ms = marks["usb_silence_audio"][1] // 1000
        self.assertTrue(PROFILED_MS <= values["profile.samples"]
                        <= served_ms - switched_ms + KEY_SETTLE_MS + KEY_HOLD_MS)
        self.assertTrue(abs(values["uptime_ms"] - served_ms) <= REPORT_START_LIMIT_MS)
        nm_text = echo_debug.run_nm(self.elf)
        profile = echo_debug.symbolise(report, nm_text, top=100000)
        self.assertTrue(profile["consistent"])
        shares = profile["shares_ms"]
        self.assertGreater(shares["code"], PROFILED_MS // 10)
        self.assertGreater(shares["idle"], PROFILED_MS // 10)
        self.assertEqual(shares["other"], 0)
        attributed = sum(row["ms"] for row in profile["functions"])
        self.assertTrue(abs(attributed - profile["shares_ms"]["code"]) <= 1)
        self.assertGreater(len(profile["functions"]), 5)
        self.assertNotIn(echo_debug.UNATTRIBUTED, [row["name"] for row in profile["functions"][:3]])
        self.assertTrue(any(row["name"].endswith(".c") for row in profile["files"]))
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        print("\n[profile] %d samples, %s; top functions: %s" % (
            profile["samples"], shares, ", ".join("%s %.0f ms" % (row["name"], row["ms"])
                                          for row in profile["functions"][:5])))

    def build_tag(self):
        return self.image[0x30:0x38].rstrip(b"\0").decode("ascii")

    def debug_volume(self, volume=None, root=(), **debug_files):
        from echo_emulator.fat_volume import Fat32Volume
        volume = volume or Fat32Volume()
        for name, data in root:
            volume.add_file(name, data, None)
        volume.add_directory(IDENTITY.data_directory)
        volume.add_directory("debug", IDENTITY.data_directory)
        for name, data in debug_files.items():
            volume.add_file(name.replace("_", "."), data, None, DEBUG_FOLDER)
        return volume

    def ejecting_session(self, volume, notes=()):
        session = self.session(volume=volume)
        machine = session.machine
        for name in ("usb_session_storage_changed", "debug_script_take", "hal_system_usb_reconnect",
                     "debug_restart_after_install", "board_restart", "updater_install",
                     "board_wait_for_power_hold", *notes):
            machine.note(name)
        machine.host_ejects()
        machine.answer_waits_for_key(4)
        session.run_to_first_frame()
        machine.vbus = True
        return session, machine

    def run_until_reconnect_request(self, machine):
        reached = machine.reached
        return machine.run(until=lambda: reached.get("hal_system_usb_reconnect", 0) >= 1,
                           until_ms=machine.milliseconds() + SCRIPT_LIMIT_MS)

    def log_time(self, machine, text, occurrence=0):
        return [stamp for stamp, event in machine.usb_log if event.startswith(text)][occurrence]

    def print_usb_log(self, title, machine, extra=()):
        events = sorted(list(machine.usb_log) + list(extra))
        print("\n[%s] USB events: %s" % (title, "; ".join("%d ms %s" % item for item in events)))

    def converted_screenshots(self, session, directory):
        volume = Path(directory) / "volume"
        session.fat().extract("/" + DEBUG_FOLDER, volume / IDENTITY.data_directory / "debug")
        return echo_debug.Tool(volume=volume).screenshots(Path(directory) / "png")["screenshots"]

    def test_a_script_runs_once_after_an_eject_plays_a_track_and_the_player_reconnects(self):
        from PIL import Image
        from echo_emulator.playback import ALBUM_COUNT, Rig
        from echo_emulator.real_volume import real_volume
        script = "\n".join(["build " + self.build_tag(), "play", "down,down,down,down,down,down",
                            "play", "play", "wait 1500", "measure 1500", "back:long", "wait 700",
                            "screenshot home", "reconnect", "play", ""])
        volume = self.debug_volume(real_volume(playable=True, album_count=ALBUM_COUNT),
                                   run_txt=script.encode("ascii"))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume)
        session, machine = rig.session, rig.machine
        marks, frames = [], []
        for name in ("usb_session_storage_changed", "debug_script_take", "hal_system_usb_reconnect"):
            machine.note(name)
        for name in ("debug_report_write", "debug_script_take", "hal_system_usb_reconnect"):
            machine.hook_symbol(name, lambda name=name: marks.append((machine.milliseconds(), name)))
        machine.hook_symbol("hal_system_usb_reconnect",
                            lambda: frames.append(list(machine.panel.frame)))
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertFalse(session.fat().exists(RUN_PATH))
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(machine.usb_connect_count, 1)
        self.assertTrue(rig.audio.plays)
        self.assertIsNotNone(rig.first_audio_after(rig.audio.plays[0]))
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertGreaterEqual(self.log_time(machine, "pull-up on", 1)
                                - self.log_time(machine, "soft disconnect"), RECONNECT_OFF_MS)
        report = self.read_report(session)
        values, ran = report["values"], report["script"]
        self.assertEqual(values["script.state"], "finished")
        self.assertEqual(values["script.interrupted_line"], 0)
        self.assertEqual([line["status"] for line in ran["lines"]], ["ok"] * 11 + ["not_run"])
        self.assertEqual([line["text"] for line in ran["lines"]], script.split("\n")[:12])
        measure = ran["measures"][0]
        self.assertEqual((measure["line"], measure["ms"], measure["audio"]), (7, 1500, 1))
        self.assertGreater(measure["frames"], 0)
        self.assertGreater(measure["frame_avg_us"], 0)
        self.assertGreater(measure["blocks"], 0)
        self.assertEqual(measure["underruns"], 0)
        self.assertGreater(measure["buffered_ms"], 0)
        self.assertGreater(measure["handoff_avg_us"], 0)
        self.assertTrue(0 <= measure["core0_busy_percent"] <= 100)
        self.assertEqual(ran["screenshots"], [{"slot": 0, "name": "home"}])
        details = [event["kind"] + " " + event["detail"] for event in report["events"]]
        self.assertIn("script finished", details)
        self.assertTrue(any(detail.startswith("player playing") for detail in details))
        with tempfile.TemporaryDirectory() as directory:
            shots = self.converted_screenshots(session, directory)
            self.assertEqual([(shot["name"], shot["width"], shot["height"]) for shot in shots],
                             [("home", WIDTH, HEIGHT)])
            picture = Image.open(shots[0]["png"]).convert("RGB")
            drawn = echo_debug.rgb888(struct.pack("<%dH" % len(frames[0]), *frames[0]), len(frames[0]))
            self.assertEqual(picture.size, (WIDTH, HEIGHT))
            self.assertEqual(picture.tobytes(), drawn)
            self.assertGreater(len(set(frames[0])), 16)
        machine.run(until_ms=machine.milliseconds() + QUIET_AFTER_MS)
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertEqual(machine.reached["hal_system_usb_reconnect"], 1)
        self.assertEqual(machine.reached["debug_script_take"], 2)
        self.assertEqual(rig.audio.underruns(rig.audio.plays[0]), [])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.print_usb_log("script run", machine, marks)
        print("[script run] measure: %s" % measure)

    def test_a_script_for_another_build_is_removed_unrun_and_the_player_still_reconnects(self):
        volume = self.debug_volume(run_txt=b"build 0000000\nplay\nreconnect\n")
        session, machine = self.ejecting_session(volume)
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertFalse(session.fat().exists(RUN_PATH))
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        self.assertEqual(report["values"]["script.state"], "refused_build")
        self.assertEqual([line["status"] for line in report["script"]["lines"]], ["not_run"] * 3)
        self.assertEqual([event["detail"] for event in report["events"] if event["kind"] == "key"], [])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def run_probe_script(self, lines):
        script = ("build %s\n%s\nreconnect\n" % (self.build_tag(), "\n".join(lines))).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        return session, machine, self.read_report(session)

    def test_peek_poke_and_gpio_lines_act_on_the_allowed_registers_and_report_what_they_saw(self):
        lines = ["peek 0x500100e8", "poke 0x5001009c 0x00ff00a5", "gpio 2.0 low", "gpio 2.0 read",
                 "gpio 2.0 high", "gpio 0.14 function 1", "gpio 0.14 input"]
        refused = ["peek 0x60080000", "poke 0x03000000 1", "poke 0x500100e9 1", "gpio 3.1 low"]
        session, machine, report = self.run_probe_script(lines + refused)
        script = report["script"]
        self.assertEqual(report["values"]["script.state"], "finished")
        self.assertEqual([line["status"] for line in script["lines"]],
                         ["ok"] * (1 + len(lines)) + ["refused"] * len(refused) + ["ok"])
        probes = script["probes"]
        self.assertEqual([probe["command"] for probe in probes], lines)
        self.assertEqual([probe["line"] for probe in probes], list(range(2, 2 + len(lines))))
        self.assertEqual([(probe["before"], probe["after"]) for probe in probes[:6]],
                         [(2, 2), (0, 0xA5), (0x011, 0x010), (0x010, 0x010), (0x010, 0x011),
                          (0x011, 0x111)])
        self.assertEqual((probes[6]["before"], probes[6]["after"] & ~1), (0x111, 0))
        self.assertEqual(probes[2]["pin_after"], {"function": 0, "output": True, "level": 0})
        self.assertEqual({probe["cable"] for probe in probes}, {1})
        self.assertEqual({probe["charge_pin"] for probe in probes}, {1})
        times = [probe["ms"] for probe in probes]
        self.assertEqual(times, sorted(times))
        self.assertIn(("grf", 0x9C, 0x00FF00A5), machine.register_log)
        port_2 = [entry[4] & 1 for entry in machine.gpio_log if entry[1] == 2 and entry[2] == GPIO_DATA]
        self.assertEqual(port_2[-2:], [0, 1])
        self.assertEqual(machine.gpio[0].regs[4] >> 14 & 1, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        print("\n[probe script]")
        for probe in probes:
            print("  %s" % probe)

    def test_without_a_script_an_ejected_session_changes_nothing_and_does_not_reconnect(self):
        session, machine = self.ejecting_session(self.debug_volume())
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        machine.run(until_ms=machine.milliseconds() + QUIET_AFTER_MS)
        self.assertEqual(machine.usb_connect_count, 1)
        self.assertNotIn("hal_system_usb_reconnect", machine.reached)
        machine.vbus = False
        machine.run(until_ms=machine.milliseconds() + 300)
        writes_before = len(session.card.writes)
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report_sectors = session.file_sectors(self.report_path(session))
        for _, lba, count in session.card.writes[writes_before:]:
            self.assertIn(lba, report_sectors)
        report = self.read_report(session)
        self.assertEqual(report["values"]["script.state"], "none")
        self.assertEqual(report["values"]["usb.last_end"], "ejected")
        self.assertEqual(report["script"],
                         {"lines": [], "measures": [], "screenshots": [], "probes": []})
        self.assertEqual(session.fat().list("/" + DEBUG_FOLDER), ["report0.txt"])
        self.assertEqual(machine.reached["debug_script_take"], 2)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_with_an_update_and_a_script_the_install_comes_first_and_the_script_waits(self):
        newer = self.newer_image()
        script = ("build %s\nwait 100\nreconnect\n" % self.build_tag()).encode("ascii")
        volume = self.debug_volume(root=[(IDENTITY.update_file, newer)], run_txt=script)
        first, machine = self.ejecting_session(volume)
        machine.stop_at("board_wait_for_power_hold")
        self.assertEqual(machine.run(until_ms=self.session_module.FRAME_LIMIT_MS),
                         "reached board_wait_for_power_hold")
        self.assertEqual(machine.reached["updater_install"], 1)
        self.assertNotIn("debug_script_take", machine.reached)
        self.assertNotIn("board_restart", machine.reached)
        self.assertIsNone(machine.reset_requested)
        self.assertEqual(first.fat().read(RUN_PATH), script)
        self.assertFalse(first.fat().exists("/" + IDENTITY.update_file))
        self.assertEqual(first.slot_image("slot_1"), newer[:self.session_module.SLOT_BYTES])
        second = first.reboot(image=first.slot_image("slot_1") + newer[-4:])
        restarted = second.machine
        for name in ("usb_session_storage_changed", "debug_script_take", "hal_system_usb_reconnect",
                     "updater_install"):
            restarted.note(name)
        restarted.host_ejects()
        restarted.vbus = True
        self.assertEqual(self.run_usb_sessions(second, 1), "condition met")
        self.assertFalse(second.fat().exists(RUN_PATH))
        self.assertEqual(self.run_until_reconnect_request(restarted), "condition met")
        self.assertEqual(self.run_usb_sessions(second, 2), "condition met")
        report = self.read_report(second)
        self.assertEqual(report["values"]["script.state"], "finished")
        self.assertEqual([line["status"] for line in report["script"]["lines"]], ["ok"] * 3)
        self.assertNotIn("updater_install", restarted.reached)
        self.assert_healthy(second)

    def test_a_restart_request_ends_a_successful_install_in_a_restart_into_the_new_image(self):
        newer = self.newer_image()
        volume = self.debug_volume(root=[(IDENTITY.update_file, newer)], restart_req=RESTART_REQUEST)
        first, machine = self.ejecting_session(volume)
        self.assertEqual(machine.run(until_ms=self.session_module.FRAME_LIMIT_MS),
                         "system reset requested")
        self.assertEqual(machine.reached["updater_install"], 1)
        self.assertEqual(machine.reached["board_restart"], 1)
        self.assertNotIn("board_wait_for_power_hold", machine.reached)
        self.assertEqual(first.fat().read(RESTART_PATH), RESTART_ANSWER)
        self.assertFalse(first.fat().exists("/" + IDENTITY.update_file))
        self.assertEqual(first.slot_image("slot_1"), newer[:self.session_module.SLOT_BYTES])
        self.assertEqual(first.slot_image("slot_2"), newer[:self.session_module.SLOT_BYTES])
        self.assertEqual([value for _, value in machine.pmu_flag_writes], [BOOT_FLAG_CLEAR])
        self.assertNotIn(LOADER_SELECT_VALUE,
                         [value for _, offset, value in machine.grf_log
                          if offset == LOADER_SELECT_REGISTER])
        self.assertEqual(machine.systick.vtor, 0)
        self.assertEqual(machine.panics, [])
        self.assertEqual(self.read_report(first)["values"]["report.reason"], "restart")
        second = first.reboot(image=first.slot_image("slot_1") + newer[-4:])
        restarted = second.machine
        for name in ("usb_session_storage_changed", "updater_install", "board_restart"):
            restarted.note(name)
        restarted.host_ejects()
        restarted.vbus = True
        restarted.cable_bit_dead = True
        self.assertEqual(self.run_usb_sessions(second, 1), "condition met")
        report = self.read_report(second)
        values = report["values"]
        self.assertEqual(values["firmware.build"], self.build_tag())
        self.assertEqual(values["usb.last_end"], "none")
        self.assertEqual((values["power.vbus"], values["power.usb_powered"]), (0, 1))
        self.assertEqual((values["loop.boot"], values["loop.connect_attempts"],
                          values["loop.connect_answered"], values["loop.last_connect"],
                          values["loop.cable_bit_at_connect"]),
                         ("loop_restart", 1, 1, "answered", 0))
        restarted.run(until_ms=restarted.milliseconds() + 1000)
        self.assertFalse(second.fat().exists(RESTART_PATH))
        self.assertEqual(restarted.usb_connect_count, 1)
        self.assertNotIn("updater_install", restarted.reached)
        self.assertNotIn("board_restart", restarted.reached)
        self.assert_healthy(second)
        self.print_usb_log("restart after install", machine)
        self.print_usb_log("after the restart", restarted)

    def test_a_reconnect_works_when_the_cable_bit_has_died_with_the_usb_power(self):
        script = ("build %s\nusb_sense kept_on\nusb_sense normal\nwait 100\nreconnect\n"
                  % self.build_tag()).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        machine.cable_bit_dies_with_usb = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.read_report(session)["values"]["power.vbus"], 1)
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertTrue(machine.cable_bit_dead)
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        values = report["values"]
        self.assertEqual(values["report.reason"], "usb_session")
        self.assertEqual(values["script.state"], "finished")
        self.assertEqual([line["status"] for line in report["script"]["lines"]], ["ok"] * 5)
        self.assertEqual((values["power.vbus"], values["power.usb_powered"]), (0, 1))
        self.assertEqual((values["usb.grf_e8"], values["usb.b_session"]), (0, 1))
        self.assertEqual((values["loop.boot"], values["loop.connect_attempts"],
                          values["loop.connect_answered"], values["loop.connect_unanswered"],
                          values["loop.last_connect"], values["loop.cable_bit_at_connect"]),
                         ("cold", 1, 1, 0, "answered", 0))
        self.assertEqual(values["usb.last_end"], "ejected")
        machine.run(until_ms=machine.milliseconds() + QUIET_AFTER_MS)
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertEqual(machine.usb_log[-1][1], "soft disconnect")
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.print_usb_log("reconnect without the cable bit", machine)

    def test_connect_usb_serves_a_host_without_the_cable_bit_and_back_stops_listening(self):
        session, machine = self.ejecting_session(self.debug_volume(), notes=("usb_power_down",))
        machine.cable_bit_dead = True
        session.run_to_home_screen()
        machine.run(until_ms=machine.milliseconds() + 3000)
        self.assertEqual(machine.usb_log, [])
        for key in [KEY_NEXT, KEY_PLAY, KEY_PLAY] + [KEY_NEXT] * ABOUT_CONNECT_USB_ROW:
            self.press(machine, key)
        pressed_ms = machine.milliseconds()
        self.press(machine, KEY_PLAY)
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertGreaterEqual(self.log_time(machine, "pull-up on"), pressed_ms + CABLE_STEADY_MS)
        values = self.read_report(session)["values"]
        self.assertEqual((values["report.reason"], values["usb.grf_e8"], values["usb.b_session"]),
                         ("usb_session", 0, 1))
        self.assertEqual((values["loop.connect_attempts"], values["loop.connect_answered"],
                          values["loop.last_connect"], values["loop.cable_bit_at_connect"]),
                         (1, 1, "answered", 0))
        machine.run(until=lambda: machine.usb_log[-1][1] == "soft disconnect",
                    until_ms=machine.milliseconds() + 1000)
        self.assertEqual(machine.usb_log[-2][1], "host ejects")
        machine.host_absent = True
        downs = machine.reached["usb_power_down"]
        machine.run(until_ms=machine.milliseconds() + RECONNECT_OFF_MS)
        self.press(machine, KEY_PLAY)
        machine.run(until_ms=machine.milliseconds() + CHARGER_MS)
        self.assertEqual((machine.usb_connect_count, machine.usb_log[-1][1]), (2, "pull-up on"))
        self.assertEqual(machine.reached["usb_power_down"], downs)
        self.press(machine, KEY_BACK)
        self.assertEqual(machine.reached["usb_power_down"], downs + 1)
        self.assertEqual(machine.usb_log[-1][1], "soft disconnect")
        self.assertEqual(len(self.reports(session)), 1)
        machine.run(until_ms=machine.milliseconds() + QUIET_AFTER_MS)
        self.assertEqual(machine.usb_connect_count, 2)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.print_usb_log("Connect USB by hand", machine)

    def test_a_reconnect_nobody_answers_powers_usb_down_and_leaves_a_report(self):
        script = ("build %s\nreconnect\n" % self.build_tag()).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        machine.cable_bit_dies_with_usb = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        machine.vbus = False
        machine.run(until_ms=machine.milliseconds() + RECONNECT_WAIT_MS + RECONNECT_OFF_MS + 2000)
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertEqual(machine.reached["usb_session_storage_changed"], 1)
        self.assertEqual(machine.usb_log[-1][1], "soft disconnect")
        values = self.read_report(session)["values"]
        self.assertEqual((values["report.reason"], values["loop.connect_attempts"],
                          values["loop.connect_answered"], values["loop.connect_unanswered"],
                          values["loop.last_connect"]), ("no_host", 1, 0, 1, "no_host"))
        self.assertEqual(values["script.state"], "finished")
        self.assertEqual(values["power.usb_powered"], 0)
        machine.run(until_ms=machine.milliseconds() + QUIET_AFTER_MS)
        self.assertEqual(machine.usb_connect_count, 2)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_a_restart_request_does_nothing_after_a_refused_or_failed_install(self):
        refused = self.session_module.with_brand(self.image, "EchoPod")
        for image, fail in ((refused, False), (self.newer_image(), True)):
            volume = self.debug_volume(root=[(IDENTITY.update_file, image)],
                                       restart_req=RESTART_REQUEST)
            session, machine = self.ejecting_session(volume)
            if fail:
                machine.stub_symbol("updater_install", lambda machine: 0)
            self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
            machine.run(until_ms=machine.milliseconds() + 1000)
            self.assertIsNone(machine.reset_requested)
            self.assertNotIn("board_restart", machine.reached)
            self.assertNotIn("debug_restart_after_install", machine.reached)
            self.assertEqual("updater_install" in machine.reached, fail)
            self.assertFalse(session.fat().exists(RESTART_PATH))
            self.assertEqual(session.fat().exists("/" + IDENTITY.update_file), fail)
            self.assertEqual(session.slot_image("slot_1"), self.image[:self.session_module.SLOT_BYTES])
            self.assertEqual(machine.panics, [])

    def hold_power(self, machine):
        machine.power_key_released_at_ms = machine.milliseconds() + POWER_HOLD_MS

    def test_a_long_power_press_saves_a_report_before_the_power_is_cut(self):
        session = self.session()
        machine = session.machine
        machine.note("debug_report_write")
        session.run_to_home_screen()
        machine.run(until_ms=COLD_REPORT_MS + 500)
        self.assertEqual(self.reports(session), [])
        self.hold_power(machine)
        self.assertEqual(machine.run(until_ms=machine.milliseconds() + 8000),
                         "firmware reached hal_power_off")
        self.assertNotIn("debug_report_write", machine.reached)
        machine.continue_past_stop()
        self.assertEqual(machine.run(until_ms=machine.milliseconds() + 5000),
                         "firmware reached board_power_cut")
        self.assertEqual(machine.reached["debug_report_write"], 1)
        self.assertTrue(session.usb_overlay_reads("slot_1"))
        values = self.read_report(session)["values"]
        self.assertEqual((values["report.sequence"], values["report.reason"],
                          values["report.cold_start"]), (1, "shutdown", 0))
        self.assertGreater(values["uptime_ms"], COLD_REPORT_MS)
        self.assertEqual(values["battery.source"], "gauge")
        self.assertEqual((values["power.vbus"], values["power.usb_powered"]), (0, 0))
        self.assertEqual(values["usb.grf_e8"], 0)
        self.assertIsNone(values["usb.gotgctl"])
        self.assertIsNone(values["usb.b_session"])
        details = [event["kind"] + " " + event["detail"] for event in self.read_report(session)["events"]]
        self.assertIn("key power long", details)
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.panics, ["hal_power_off", "board_power_cut"])
        self.assert_only_the_user_volume_changed(session)

    def test_switching_off_on_usb_power_saves_a_report_on_the_way_into_charge_mode(self):
        session, machine = self.ejecting_session(self.debug_volume())
        machine.note("debug_report_write")
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        machine.run(until_ms=COLD_REPORT_MS + 500)
        self.hold_power(machine)
        machine.run(until=lambda: machine.reached.get("debug_report_write", 0) >= 2,
                    until_ms=machine.milliseconds() + 8000)
        machine.run(until_ms=machine.milliseconds() + 1000)
        newest, older = self.reports(session)
        self.assertEqual((newest[1]["values"]["report.sequence"], newest[1]["values"]["report.reason"],
                          newest[1]["values"]["report.cold_start"]), (2, "shutdown", 0))
        self.assertEqual((older[1]["values"]["report.reason"], older[1]["values"]["report.cold_start"]),
                         ("usb_session", 1))
        self.assertNotEqual(newest[0], older[0])
        self.assertEqual(newest[1]["values"]["power.vbus"], 1)
        self.assertEqual(newest[1]["values"]["usb.last_end"], "ejected")
        self.assertEqual(machine.panics, [])
        self.assertIsNone(machine.reset_requested)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def cold_session_on(self, earlier):
        session = earlier.reboot()
        machine = session.machine
        machine.note("usb_session_storage_changed")
        machine.answer_waits_for_key(4)
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        return session

    def test_a_cold_start_report_never_overwrites_the_report_before_it(self):
        first = self.session()
        machine = first.machine
        machine.note("usb_session_storage_changed")
        machine.answer_waits_for_key(4)
        first.run_to_home_screen()
        machine.run(until_ms=COLD_REPORT_MS + 500)
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(first, 1), "condition met")
        kept_path, kept = self.reports(first)[0]
        kept_bytes = first.fat().read(kept_path)
        self.assertEqual((kept["values"]["report.sequence"], kept["values"]["report.cold_start"]), (1, 0))
        second = self.cold_session_on(first)
        cold_path, cold = self.reports(second)[0]
        values = cold["values"]
        self.assertEqual((values["report.sequence"], values["report.cold_start"]), (2, 1))
        self.assertNotEqual(cold_path, kept_path)
        self.assertGreaterEqual(values["uptime_ms"], CABLE_STEADY_MS)
        self.assertLess(values["uptime_ms"], 2000)
        self.assertEqual(values["start.warm"], 0)
        self.assertEqual((values["usb.grf_e8_start"], values["usb.grf_e8"]), (2, 2))
        self.assertEqual(values["battery.source"], "gauge")
        self.assertEqual((values["battery.millivolts"], values["battery.usb_power"]), (3999, 1))
        self.assertEqual((values["power.adc_source"], values["power.adc_raw"]), ("gauge", 819))
        self.assertEqual(values["power.gauge_readings"], 1)
        self.assertEqual(values["power.gauge_millivolts"], 3999)
        third = self.cold_session_on(second)
        newest, older = self.reports(third)
        self.assertEqual((newest[0], newest[1]["values"]["report.sequence"]), (cold_path, 3))
        self.assertEqual(older[0], kept_path)
        self.assertEqual(third.fat().read(kept_path), kept_bytes)
        self.assertEqual(len(third.fat().list("/" + DEBUG_FOLDER)), 2)
        with tempfile.TemporaryDirectory() as directory:
            volume = Path(directory) / "volume"
            third.fat().extract("/" + DEBUG_FOLDER, volume / IDENTITY.data_directory / "debug")
            tool = echo_debug.Tool(volume=volume)
            status = tool.status()
            self.assertTrue(status["cold_start_only"])
            self.assertIn("advice", status)
            listed = tool.report()["reports"]
            self.assertEqual([(item["sequence"], item["cold_start"], item["reason"]) for item in listed],
                             [(3, True, "usb_session"), (1, False, "usb_session")])
        self.assert_only_the_user_volume_changed(third)
        self.assert_healthy(third)

    def test_a_stale_restart_request_is_removed_without_an_install(self):
        session, machine = self.ejecting_session(self.debug_volume(restart_req=RESTART_REQUEST))
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        machine.run(until_ms=machine.milliseconds() + 1000)
        self.assertFalse(session.fat().exists(RESTART_PATH))
        self.assertIsNone(machine.reset_requested)
        self.assertNotIn("board_restart", machine.reached)
        self.assert_healthy(session)

    def run_usb_sessions(self, session, count):
        reached = session.machine.reached
        return session.machine.run(
            until=lambda: reached.get("usb_session_storage_changed", 0) >= count,
            until_ms=session.machine.milliseconds() + self.session_module.FRAME_LIMIT_MS)

    def test_a_second_usb_session_reuses_the_usb_overlay_still_in_memory(self):
        session = self.session()
        machine = session.machine
        for name in ("usb_storage_session", "usb_session_storage_changed",
                     "hal_system_return_scratch"):
            machine.note(name)
        machine.answer_waits_for_key(4)
        executed = machine.count_window_code()
        machine.run(until=lambda: "hal_system_return_scratch" in machine.reached,
                    until_ms=self.session_module.FRAME_LIMIT_MS)
        self.assertGreater(executed[0], 0)
        self.assertEqual(session.usb_overlay_reads("slot_1"), [])
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        loads = session.usb_overlay_reads("slot_1")
        self.assertTrue(loads)
        machine.run(until_ms=machine.milliseconds() + 200)
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        self.assertEqual(machine.reached["usb_storage_session"], 2)
        self.assertEqual(session.usb_overlay_reads("slot_1"), loads)
        self.assertEqual(session.usb_overlay_reads("slot_2"), [])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_a_damaged_usb_overlay_in_slot_1_is_replaced_by_the_copy_in_slot_2(self):
        session = self.session(files=[("DBLSHARP.IMG", self.image, None)], vbus=True)
        session.damage_usb_overlay("slot_1")
        machine = session.machine
        machine.note("updater_check")
        session.run_to_first_frame()
        self.assertEqual(session.run_through_update_check(),
                         "reached usb_session_storage_changed")
        self.assertEqual(machine.reached["updater_check"], 1)
        self.assertTrue(session.usb_overlay_reads("slot_1"))
        self.assertTrue(session.usb_overlay_reads("slot_2"))
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_without_intact_usb_code_the_session_says_so_and_nothing_from_it_runs(self):
        session = self.session(files=[("DBLSHARP.IMG", self.newer_image(), None)], vbus=True)
        session.damage_usb_overlay("slot_1")
        session.damage_usb_overlay("slot_2")
        machine = session.machine
        for name in ("updater_check", "updater_install", "usb_storage_session", "usb_power_down"):
            machine.note(name)
        notices = machine.collect_notices()
        session.run_to_first_frame()
        machine.run(until=lambda: session.usb_overlay_reads("slot_1"),
                    until_ms=self.session_module.FRAME_LIMIT_MS)
        executed = machine.count_window_code()
        self.assertEqual(session.run_until_waiting_for_key(), "reached board_wait_for_key")
        self.assertEqual(notices[-1], (english("STR_USB"), english("STR_USB_CODE_MISSING")))
        self.assertGreaterEqual(machine.reached["usb_power_down"], 1)
        for name in ("updater_check", "updater_install", "usb_storage_session"):
            self.assertNotIn(name, machine.reached)
        self.assertEqual(executed[0], 0)
        self.assertFalse(any(machine.read_memory(make_image.OVERLAY_BASE,
                                                 make_image.OVERLAY_CAPACITY)))
        self.assertTrue(session.file_exists("DBLSHARP.IMG"))
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def loaded_language_pack(self, session):
        pack = make_image.language_packs_in(self.image)[0]
        return session.machine.read_memory(LANGUAGE_RAM, len(pack)) == pack

    def test_the_text_is_read_from_the_language_pack_in_slot_1(self):
        session = self.session()
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(self.loaded_language_pack(session))
        self.assertTrue(session.language_pack_reads("slot_1"))
        self.assertEqual(session.language_pack_reads("slot_2"), [])
        self.assert_healthy(session)

    def test_a_damaged_language_pack_in_slot_1_is_replaced_by_the_copy_in_slot_2(self):
        session = self.session()
        session.damage_language_pack("slot_1")
        notices = session.machine.collect_notices()
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(self.loaded_language_pack(session))
        self.assertTrue(session.language_pack_reads("slot_1"))
        self.assertTrue(session.language_pack_reads("slot_2"))
        self.assertNotIn(MISSING_TEXT, [message for _, message in notices])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_without_an_intact_language_pack_the_player_says_so_and_still_starts(self):
        session = self.session()
        session.damage_language_pack("slot_1")
        session.damage_language_pack("slot_2")
        machine = session.machine
        notices = machine.collect_notices()
        machine.answer_waits_for_key(1)
        machine.note("app_step")
        reason = machine.run(until=lambda: machine.reached.get("app_step", 0) >= STEPS_AFTER_NOTICE,
                             until_ms=self.session_module.FRAME_LIMIT_MS)
        self.assertEqual(reason, "condition met")
        self.assertEqual(notices[0], (IDENTITY.short_name, MISSING_TEXT))
        self.assertEqual(machine.reached["board_wait_for_key"], 1)
        self.assertFalse(self.loaded_language_pack(session))
        self.assertTrue(session.language_pack_reads("slot_1"))
        self.assertTrue(session.language_pack_reads("slot_2"))
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_internal_storage_is_read_with_multiple_block_commands_that_are_stopped(self):
        session = self.session()
        session.run_to_home_screen()
        indexes = [index for _, index, _ in session.card.commands]
        self.assertIn(18, indexes)
        for position, index in enumerate(indexes):
            if index == 18:
                self.assertEqual(indexes[position + 1], 12)

    def realistic_boot(self, sectors_per_cluster, next_free, usb_ms, gap_ms):
        from echo_emulator.real_volume import real_volume
        volume = real_volume(sectors_per_cluster=sectors_per_cluster, next_free=next_free)
        session = self.session(volume=volume)
        machine = session.machine
        machine.vbus = True
        listens = machine.trace("usb_session_listen")
        steps = machine.trace("app_step")
        machine.answer_waits_for_key(5)
        self.assertEqual(session.run_to_first_frame(), "condition met")
        self.assertLess(machine.milliseconds(), FIRST_FRAME_LIMIT_MS)
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(listens)
        self.assertGreaterEqual(listens[0], CABLE_STEADY_MS)
        self.assertLess(listens[0], usb_ms)
        self.assertLess(machine.milliseconds(), 20000)
        gaps = [later - earlier for earlier, later in zip(steps, steps[1:])]
        self.assertLess(max(gaps), gap_ms)
        self.assertEqual(machine.watchdog_resets, 0)
        self.assertGreater(machine.watchdog_kicks, 0)
        self.assert_healthy(session)
        summary("realistic %d sectors per cluster" % sectors_per_cluster, session)

    def test_a_cold_boot_on_a_3_4_gib_volume_lights_the_screen_and_serves_usb_quickly(self):
        self.realistic_boot(8, None, 1500, 3000)

    def test_a_cold_boot_with_512_byte_clusters_and_a_stale_hint_stays_responsive(self):
        self.realistic_boot(1, 2, 5000, 6000)

    def playback_rig(self):
        from echo_emulator.playback import Rig
        return Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf)

    def test_a_deep_cue_track_starts_quickly_and_switching_tracks_keeps_the_ui_smooth(self):
        rig = self.playback_rig()
        machine = rig.machine
        rig.open_songs()
        mark = rig.play_song(DEEP_TRACK)
        started = machine.milliseconds()
        rig.run_for(2300)
        audible = rig.first_audio_after(mark)
        self.assertIsNotNone(audible)
        self.assertLess(audible - mark, FIRST_AUDIO_LIMIT_MS)
        self.assertLess(rig.longest_gap(mark), TRACK_GAP_LIMIT_MS)
        fade_end = max(stamp for stamp, _ in rig.audio.volumes if stamp >= mark)
        self.assertLess(fade_end - mark, FADE_LIMIT_MS)
        window = (started + 1000, started + 2000)
        self.assertLessEqual(rig.fat_sector_reads(*window), FAT_READS_PER_SECOND_LIMIT)
        self.assertGreaterEqual(rig.data_sector_reads(*window), DATA_READS_PER_SECOND_MINIMUM)
        plays = len(rig.audio.plays)
        switched = machine.milliseconds()
        rig.skip("next")
        self.assertGreater(len(rig.audio.plays), plays)
        rig.press("back")
        rig.scroll("next", 8)
        rig.run_for(300)
        self.assertLess(rig.longest_gap(switched), TRACK_GAP_LIMIT_MS)
        self.assertEqual(rig.audio.underruns(audible), [])
        self.assertEqual(machine.panics, [])
        self.assert_healthy(rig.session)
        summary("deep cue track", rig.session)


def prerequisites_missing():
    if importlib.util.find_spec("unicorn") is None:
        return "unicorn is not installed"
    for path in (CONFIG["image"], CONFIG["elf"]):
        if not Path(path).exists():
            return "%s is missing" % path
    return None


def parse_arguments(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, default=DEFAULT_IMAGE)
    parser.add_argument("--elf", type=Path, default=DEFAULT_ELF)
    parser.add_argument("--usb-overlay-elf", type=Path, default=DEFAULT_USB_OVERLAY_ELF)
    parser.add_argument("tests", nargs="*")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    CONFIG["image"] = arguments.image
    CONFIG["elf"] = arguments.elf
    CONFIG["usb_overlay_elf"] = arguments.usb_overlay_elf
    reason = prerequisites_missing()
    if reason:
        print("SKIP: " + reason)
        return SKIP_EXIT_CODE
    suite = unittest.defaultTestLoader.loadTestsFromNames(
        arguments.tests, sys.modules[__name__]) if arguments.tests else (
        unittest.defaultTestLoader.loadTestsFromTestCase(EchoImageTest))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
