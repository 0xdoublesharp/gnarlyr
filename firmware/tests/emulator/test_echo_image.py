import argparse
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))

import make_image

SKIP_EXIT_CODE = 77
DEFAULT_IMAGE = max((PROJECT / "build" / "device").glob("ECHOV*.IMG"), default=PROJECT / "build" / "device" / "ECHOV454.IMG")
DEFAULT_ELF = PROJECT / "build" / "device" / "platform" / "device" / "echo_device.elf"
DEFAULT_USB_OVERLAY_ELF = DEFAULT_ELF.with_name("echo_usb_overlay.elf")
CONFIG = {"image": DEFAULT_IMAGE, "elf": DEFAULT_ELF, "usb_overlay_elf": DEFAULT_USB_OVERLAY_ELF}

FIRST_FRAME_LIMIT_MS = 1000
DEEP_TRACK = 12
FIRST_AUDIO_LIMIT_MS = 1500
TRACK_GAP_LIMIT_MS = 1000
FADE_LIMIT_MS = 1500
FAT_READS_PER_SECOND_LIMIT = 40
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


def settings_file(orientation):
    data = bytearray(SETTINGS_CHECKSUM_FIELD + 4)
    data[0:4] = b"EMST"
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

    def session(self, files=(), image=None, vbus=False, volume=None):
        return self.session_module.Session(image or self.image, self.elf, files=files,
                                           vbus_after_first_frame=vbus, volume=volume,
                                           usb_overlay_elf=self.usb_overlay_elf)

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
        volume.add_directory(".echo")
        volume.add_file("settings.bin", settings_file(orientation), directory=".echo")
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

    def test_power_hold_comes_first_and_the_power_pair_follows(self):
        session = self.session()
        session.run_to_home_screen()
        machine = session.machine
        hold = pin_high_index(machine, 2, 6)
        pair_a = pin_high_index(machine, 0, 14)
        pair_b = pin_high_index(machine, 2, 0)
        self.assertIsNotNone(hold)
        self.assertIsNotNone(pair_a)
        self.assertIsNotNone(pair_b)
        self.assertLess(hold, pair_a)
        self.assertLess(hold, pair_b)
        self.assertEqual(machine.gpio[2].regs[GPIO_DATA] >> 6 & 1, 1)
        self.assertEqual(machine.gpio[2].regs[4] >> 6 & 1, 1)
        self.assertEqual(machine.gpio[0].regs[GPIO_DATA] >> 14 & 1, 1)
        self.assertEqual(machine.gpio[2].regs[GPIO_DATA] & 1, 1)
        self.assertFalse(pin_ever_output(machine, 2, 1))

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
        for data in session.disk.sectors.values():
            if data[:4] == b"EMDB":
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

    def test_a_newer_podium_image_is_installed_slot_2_first_and_header_last(self):
        newer = self.newer_image()
        session = self.session(files=[("ECHOV498.IMG", newer, None)], vbus=True)
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
        session = self.session(files=[("ECHOV498.IMG", self.image, None)], vbus=True)
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
        first = self.session(files=[("ECHOV498.IMG", newer, None)], vbus=True)
        cluster = first.volume.first_cluster("ECHOV498.IMG")
        self.assertEqual(self.boot_until_waiting_for_key(first),
                         "reached board_wait_for_power_hold")
        self.assertEqual(first.machine.reached["updater_install"], 1)
        self.assertFalse(first.file_exists("ECHOV498.IMG"))
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
        first = self.session(files=[("ECHOV498.IMG", newer, None)], vbus=True)
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
        for name, data in (("ECHOV497.IMG", mini), ("ECHOV498.IMG", self.image)):
            session = self.session(files=[(name, data, None)], vbus=True)
            self.boot_until_waiting_for_key(session)
            self.assertNotIn("updater_install", session.machine.reached)
            self.assertFalse(session.file_exists(name), name)
            self.assert_only_the_user_volume_changed(session)

    def test_the_usb_session_and_the_update_check_run_from_the_usb_overlay(self):
        session = self.session(files=[("ECHOV498.IMG", self.image, None)], vbus=True)
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
        session = self.session(files=[("ECHOV498.IMG", self.image, None)], vbus=True)
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
        session = self.session(files=[("ECHOV498.IMG", self.newer_image(), None)], vbus=True)
        session.damage_usb_overlay("slot_1")
        session.damage_usb_overlay("slot_2")
        machine = session.machine
        for name in ("updater_check", "updater_install", "usb_storage_session", "usb_power_down"):
            machine.note(name)
        notices = machine.collect_notices()
        session.run_to_first_frame()
        executed = machine.count_window_code()
        self.assertEqual(session.run_until_waiting_for_key(), "reached board_wait_for_key")
        self.assertEqual(notices[-1], ("USB", "USB code missing: reinstall the update"))
        self.assertGreaterEqual(machine.reached["usb_power_down"], 1)
        for name in ("updater_check", "updater_install", "usb_storage_session"):
            self.assertNotIn(name, machine.reached)
        self.assertEqual(executed[0], 0)
        self.assertFalse(any(machine.read_memory(make_image.OVERLAY_BASE,
                                                 make_image.OVERLAY_CAPACITY)))
        self.assertTrue(session.file_exists("ECHOV498.IMG"))
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
        session = self.session(volume=volume, vbus=True)
        machine = session.machine
        listens = machine.trace("usb_session_listen")
        steps = machine.trace("app_step")
        machine.answer_waits_for_key(5)
        self.assertEqual(session.run_to_first_frame(), "condition met")
        self.assertLess(machine.milliseconds(), FIRST_FRAME_LIMIT_MS)
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(listens)
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
