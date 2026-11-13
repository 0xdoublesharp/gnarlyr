import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, IDENTITY, main, settings_file


STATUS_PIXELS = 480 * 30
HOME_SETTLED_MS = 2200
WIREFRAME_FRAMES_MINIMUM = 10
PLAIN_SPLASH_MS = 1100
SCAN_SETTLE_MS = 2000
TAP_DELAY_MS = 100
TAP_MS = 150
TAP_SETTLE_MS = 350


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
            volume.add_file('settings.bin', power_settings(press), directory=IDENTITY.data_directory)
            session = self.session(volume=volume, vbus_at_start=True)
            machine = session.machine
            machine.host_absent = True
            machine.power_key_released_at_ms = 0
            machine.note('hal_system_return_scratch')
            stops = machine.trace('app_show_splash')
            charge_frames = machine.trace('charge_art_draw')
            self.assertEqual('condition met', session.run_to_first_frame())
            self.assertEqual('condition met',
                             machine.run(until=lambda: 'hal_system_return_scratch' in machine.reached,
                                         until_ms=self.session_module.FRAME_LIMIT_MS))
            scanned = len(charge_frames)
            self.assertEqual('condition met',
                             machine.run(until=lambda: len(charge_frames) > scanned,
                                         until_ms=machine.milliseconds() + SCAN_SETTLE_MS))
            before = len(stops)
            frames = len(charge_frames)
            tap = machine.milliseconds() + TAP_DELAY_MS
            machine.power_key_pressed_at_ms = tap
            machine.power_key_released_at_ms = tap + TAP_MS
            machine.run(until_ms=tap + TAP_MS + TAP_SETTLE_MS)
            self.assertEqual(before + int(press), len(stops))
            if not press:
                self.assertGreater(len(charge_frames), frames)
            self.assert_healthy(session)

    def test_the_wireframe_start_shows_home_sooner_than_the_plain_splash(self):
        session = self.playback_rig().session.reboot()
        machine, panel = session.machine, session.panel
        splash = machine.trace('app_show_splash')
        frames = machine.trace('wire_boot_fade')
        changes = []

        def watch():
            if len(panel.windows) != len(changes):
                changes.append((machine.milliseconds(), hash(tuple(panel.frame[STATUS_PIXELS:]))))
            return False

        machine.run(until=watch, until_ms=HOME_SETTLED_MS)
        home = next(ms for ms, digest in changes if digest == changes[-1][1])
        print('\n[boot] splash %d ms, %d wireframe frames, home %d ms, %d ms after the splash began'
              % (splash[0], len(frames), home, home - splash[0]))
        self.assertGreaterEqual(len(frames), WIREFRAME_FRAMES_MINIMUM)
        self.assertLess(home - splash[0], PLAIN_SPLASH_MS)
        self.assert_healthy(session)


if __name__ == '__main__':
    sys.exit(main(EchoImageTest))
