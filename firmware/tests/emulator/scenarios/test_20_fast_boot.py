import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, IDENTITY, main, settings_file


def power_settings(press):
    return settings_file(0, b38=7, b51=0x10 if press else 0)


class EchoImageTest(EchoImageHarness):
    def test_optional_extension_load_waits_until_the_first_frame(self):
        session = self.session()
        self.assertEqual('condition met', session.run_to_first_frame())
        self.assertEqual([], session.module_reads('slot_1', 8))
        self.assertEqual([], session.module_reads('slot_2', 8))
        self.assertEqual('condition met', session.run_to_home_screen())
        self.assertTrue(session.module_reads('slot_1', 8))
        self.assert_healthy(session)

    def test_power_is_latched_within_ten_milliseconds_of_reset(self):
        session = self.session()
        self.assertEqual('condition met', session.run_to_first_frame())
        writes = session.machine.gpio_log
        held = next(cycles for cycles, port, _, _, data, direction, _ in writes
                    if port == 2 and data & direction & (1 << 6))
        self.assertLessEqual(held, 24_000 * 10)

    def test_press_leaves_cable_charging_and_hold_keeps_a_tap_in_charge_mode(self):
        from echo_emulator.fat_volume import Fat32Volume
        for press in (False, True):
            volume = Fat32Volume()
            volume.add_directory(IDENTITY.data_directory)
            volume.add_file('settings.bin', power_settings(press), 4096, IDENTITY.data_directory)
            session = self.session(volume=volume, vbus_at_start=True)
            machine = session.machine
            machine.power_key_released_at_ms = 0
            stops = machine.trace('app_show_splash')
            self.assertEqual('condition met', session.run_to_first_frame())
            machine.run(until_ms=machine.milliseconds() + 2000)
            before = len(stops)
            machine.power_key_released_at_ms = machine.milliseconds() + 150
            machine.run(until_ms=machine.milliseconds() + 500)
            self.assertEqual(before + int(press), len(stops))
            self.assert_healthy(session)


if __name__ == '__main__':
    main()
