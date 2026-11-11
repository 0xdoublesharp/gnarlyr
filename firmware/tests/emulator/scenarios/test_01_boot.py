import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    BACKLIGHT_HZ,
    GPIO_DATA,
    HEIGHT,
    KEY_HOLD_MS,
    KEY_NEXT,
    LOADER_FLAG_REGISTER,
    LOADER_FLAG_VALUE,
    LOADER_SELECT_REGISTER,
    LOADER_SELECT_VALUE,
    MADCTL_CHOICES,
    MADCTL_MIRROR_COLUMNS,
    MADCTL_MIRROR_ROWS,
    PWM_CONTROL,
    PWM_PERIOD,
    PWM_RUN,
    WIDTH,
    expected_init,
    pin_ever_output,
    pin_high_index,
    summary,
)


class EchoImageTest(EchoImageHarness):
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
                                              usb_overlay_elf=self.usb_overlay_elf,
                                              module_elfs=self.module_elfs)
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


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
