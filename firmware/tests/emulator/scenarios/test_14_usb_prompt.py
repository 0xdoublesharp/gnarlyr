import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    BACKLIGHT_PIN,
    CABLE_STEADY_MS,
    CHARGER_MS,
    GPIO_DATA,
    PROMPT_NOTICED_MS,
    USB_PROMPT_MS,
)


class EchoImageTest(EchoImageHarness):
    def test_a_cable_plugged_in_while_the_player_runs_asks_before_it_connects(self):
        session, machine, asked = self.asked_session()
        session.run_to_home_screen()
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: asked,
                                     until_ms=machine.milliseconds() + CABLE_STEADY_MS + 2000),
                         "condition met")
        machine.run(until_ms=machine.milliseconds() + PROMPT_NOTICED_MS)
        self.assertEqual([event for _, event in machine.usb_log],
                         ["soft disconnect", "pull-up on, host resets the bus", "soft disconnect"])
        self.assertEqual(machine.gpio[2].regs[GPIO_DATA] >> BACKLIGHT_PIN & 1, 1)
        machine.run(until_ms=machine.milliseconds() + CHARGER_MS)
        self.assert_asked_without_enumerating(session, asked, 1)
        self.assertEqual(machine.usb_connect_count, 1)
        self.choose_file_transfer(machine)
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertEqual(len(asked), 1)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.print_usb_log("asked, then file transfer", machine)

    def test_charging_without_the_views_module_uses_no_usb_overlay(self):
        session = self.session(vbus_at_start=True)
        machine = session.machine
        machine.power_key_released_at_ms = 0
        machine.host_absent = True
        number, _ = self.module(machine, "views")
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, number, 600, lambda byte: byte ^ 0xFF)
        frames = machine.trace("usb_screen_draw_fallback")
        self.assertEqual(machine.run(until=lambda: frames,
                                     until_ms=self.session_module.FRAME_LIMIT_MS), "condition met")
        self.assertEqual(session.usb_overlay_reads("slot_1"), [])
        self.assertEqual(session.usb_overlay_reads("slot_2"), [])
        self.assertTrue(session.module_reads("slot_1", number))
        self.assertTrue(session.module_reads("slot_2", number))
        self.assert_healthy(session)

    def test_listening_for_a_host_while_playing_keeps_the_usb_overlay_unloaded(self):
        rig = self.playback_rig()
        machine = rig.machine
        rig.open_songs()
        rig.play_song(1)
        self.assertTrue(rig.audio.running)
        machine.host_absent = True
        machine.vbus = True
        rig.run_for(CABLE_STEADY_MS + 500)
        self.assertEqual(rig.session.usb_overlay_reads("slot_1"), [])
        self.assertEqual(rig.session.usb_overlay_reads("slot_2"), [])
        self.assertFalse(machine.overlays[0].loaded(machine))
        self.assertTrue(rig.audio.running)
        self.assert_healthy(rig.session)

    def test_on_the_charge_screen_only_a_host_wakes_the_prompt_and_silence_keeps_charging(self):
        session, machine, asked, charge_frames, dark = self.charging_on_a_charger()
        machine.host_arrives()
        self.assertEqual(machine.run(until=lambda: asked,
                                     until_ms=machine.milliseconds() + PROMPT_NOTICED_MS),
                         "condition met")
        machine.run(until_ms=machine.milliseconds() + PROMPT_NOTICED_MS)
        self.assertEqual(machine.gpio[2].regs[GPIO_DATA] >> BACKLIGHT_PIN & 1, 1)
        self.assert_asked_without_enumerating(session, asked, 1)
        darkened = len(dark)
        machine.run(until_ms=asked[0] + USB_PROMPT_MS + PROMPT_NOTICED_MS)
        self.assertEqual(len(dark), darkened + 1)
        self.assertGreaterEqual(dark[-1], asked[0] + USB_PROMPT_MS)
        drawn = len(charge_frames)
        machine.run(until_ms=machine.milliseconds() + CHARGER_MS)
        self.assertEqual(len(charge_frames), drawn)
        self.assertEqual(len(dark), darkened + 1)
        self.assert_asked_without_enumerating(session, asked, 1)
        self.assertEqual(machine.usb_connect_count, 1)
        self.assert_healthy(session)
        machine.vbus = False
        machine.run(until_ms=machine.milliseconds() + PROMPT_NOTICED_MS)
        self.assertEqual(machine.panics, ["hal_power_off"])
        self.print_usb_log("charge screen, host, quiet prompt", machine)

    def test_on_the_charge_screen_file_transfer_in_the_prompt_starts_the_session(self):
        session, machine, asked, charge_frames, dark = self.charging_on_a_charger()
        machine.host_arrives()
        self.assertEqual(machine.run(until=lambda: asked,
                                     until_ms=machine.milliseconds() + PROMPT_NOTICED_MS),
                         "condition met")
        machine.run(until_ms=machine.milliseconds() + PROMPT_NOTICED_MS)
        self.assert_asked_without_enumerating(session, asked, 1)
        self.choose_file_transfer(machine)
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertEqual(len(asked), 1)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.print_usb_log("charge screen, file transfer", machine)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
