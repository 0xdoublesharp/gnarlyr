import argparse
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))

SKIP_EXIT_CODE = 77
DEFAULT_IMAGE = next((PROJECT / "build" / "device-echo").glob("ECHOV*.IMG"), PROJECT / "build" / "device-echo" / "ECHOV450.IMG")
DEFAULT_ELF = PROJECT / "build" / "device-echo" / "platform" / "device" / "echo_device.elf"
CONFIG = {"image": DEFAULT_IMAGE, "elf": DEFAULT_ELF}

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

    def session(self, files=(), image=None, vbus=False):
        return self.session_module.Session(image or self.image, self.elf, files=files,
                                           vbus_after_first_frame=vbus)

    def assert_healthy(self, session):
        machine = session.machine
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.panics, [])

    def assert_no_internal_storage_change(self, session):
        self.assertEqual(session.card.writes, [])
        self.assertEqual(session.changed_sectors(), [])

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
        self.assertEqual((clear.windows, clear.nonzero), (23, 0))
        self.assertEqual(first.windows, 10)
        self.assertEqual(first.nonzero, WIDTH * HEIGHT)
        self.assertEqual(panel.painted_pixels(), WIDTH * HEIGHT)
        self.assert_windows_inside_panel(panel)
        self.assert_healthy(session)
        summary("first frame", session)

    def test_normal_boot_reaches_the_home_screen_without_touching_storage(self):
        session = self.session()
        self.assertEqual(session.run_to_home_screen(), "condition met")
        machine = session.machine
        panel = session.panel
        self.assertEqual(len(panel.lit_sweeps()), 2)
        self.assertGreater(panel.colors(), 8)
        self.assert_windows_inside_panel(panel)
        self.assert_no_internal_storage_change(session)
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
        self.assert_no_internal_storage_change(session)

    def assert_every_write_read_back(self, accesses):
        for index, access in enumerate(accesses):
            if access[0] != "W":
                continue
            lba, count = access[1], access[2]
            following = accesses[index + 1:index + 1 + count]
            self.assertEqual([item[:2] for item in following], [("R", lba + step)
                                                                 for step in range(count)])
            self.assertTrue(all(item[2] == 1 for item in following))

    def newer_image(self):
        return self.session_module.with_date(self.image, (2099, 1, 1))

    def test_a_newer_podium_image_is_installed_slot_2_first_and_header_last(self):
        newer = self.newer_image()
        session = self.session(files=[("ECHOV498.IMG", newer, None)], vbus=True)
        session.machine.note("updater_install")
        self.assertEqual(session.run_to_first_frame(), "condition met")
        self.assertEqual(session.run_until_waiting_for_key(presses=1),
                         "reached board_wait_for_key")
        machine = session.machine
        self.assert_healthy(session)
        self.assertEqual(machine.reached["updater_install"], 1)
        self.assertGreaterEqual(machine.usb_connect_count, 1)
        slots = {name: session.layout[name] for name in ("slot_1", "slot_2")}
        inside = {name: range(start, start + SLOT_SECTORS) for name, start in slots.items()}
        writes = session.card.writes
        for _, lba, count in writes:
            self.assertEqual(count, CHUNK_SECTORS)
            self.assertTrue(any(lba in span and lba + count <= span.stop
                                for span in inside.values()), hex(lba))
        for lba in session.changed_sectors():
            self.assertTrue(any(lba in span for span in inside.values()), hex(lba))
        by_slot = {name: [item for item in writes if item[1] in span]
                   for name, span in inside.items()}
        self.assertEqual(len(writes), len(by_slot["slot_1"]) + len(by_slot["slot_2"]))
        self.assertLess(max(stamp for stamp, _, _ in by_slot["slot_2"]),
                        min(stamp for stamp, _, _ in by_slot["slot_1"]))
        header_word = struct.unpack_from("<I", newer)[0]
        writes_in_order = [(lba, count, word) for kind, lba, count, *rest in session.card.accesses
                           if kind == "W" for word in rest]
        for name, start in slots.items():
            sequence = [item for item in writes_in_order if item[0] in inside[name]]
            self.assertEqual(sequence[0], (start, CHUNK_SECTORS, 0xFFFFFFFF))
            self.assertEqual(sequence[-1], (start, CHUNK_SECTORS, header_word))
            self.assertNotIn(start, [lba for lba, _, _ in sequence[1:-1]])
        self.assert_every_write_read_back(session.card.accesses)
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
                         "reached usb_session_card_changed")
        self.assertEqual(session.machine.reached["updater_check"], 1)
        self.assertNotIn("updater_install", session.machine.reached)
        self.assert_no_internal_storage_change(session)
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
        self.assert_no_internal_storage_change(session)
        self.assert_healthy(session)
        summary("refused " + update[0], session)


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
    parser.add_argument("tests", nargs="*")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    CONFIG["image"] = arguments.image
    CONFIG["elf"] = arguments.elf
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
