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
    DAC_MEASURE_MS,
    DAC_MENU_COMPUTER_ROW,
    DAC_RATE,
    KEY_MENU,
    KEY_NEXT,
    KEY_PLAY,
    LEAVE_HOLD_MS,
)


class EchoImageTest(EchoImageHarness):
    def test_the_core_clock_stays_undivided_while_the_usb_core_is_powered(self):
        session, machine, asked = self.asked_session()
        powered = []
        machine.hook_symbol("usb_power_up", lambda: powered.append(["up", machine.milliseconds()]))
        machine.hook_symbol("usb_power_down", lambda: powered.append(["down", machine.milliseconds()]))
        session.run_to_home_screen()
        machine.run(until_ms=machine.milliseconds() + 2000)
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: asked,
                                     until_ms=machine.milliseconds() + CABLE_STEADY_MS + 2000),
                         "condition met")
        self.assertTrue(any(event == "up" for event, _ in powered))

        def divider_at(milliseconds):
            dividers = [divider for at, divider in machine.coast_log if at <= milliseconds]
            return dividers[-1] if dividers else 1

        self.assertEqual([divider_at(at) for event, at in powered if event == "up"],
                         [1] * len([event for event, _ in powered if event == "up"]))
        spans = []
        for event, at in powered:
            if event == "up":
                spans.append([at, None])
            elif spans and spans[-1][1] is None:
                spans[-1][1] = at
        for start, end in spans:
            self.assertEqual([divider for at, divider in machine.coast_log
                              if at > start and (end is None or at < end) and divider > 1], [])
        self.assert_healthy(session)

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
        self.choose_connect(machine)
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertEqual(len(asked), 1)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.print_usb_log("asked, then connect to computer", machine)

    def test_charging_source_forgets_a_host_before_a_later_wall_charger(self):
        session, machine, asked = self.asked_session()
        session.run_to_home_screen()
        machine.stub_symbol("app_step", lambda machine: None)
        source = machine.symbols["charging_host"]
        host_seen = lambda: bool(machine.uc.mem_read(source, 1)[0])
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: asked,
                                     until_ms=machine.milliseconds() + CABLE_STEADY_MS + 2000),
                         "condition met")
        self.assertTrue(host_seen())
        machine.vbus = False
        self.assertEqual(machine.run(until=lambda: not host_seen(),
                                     until_ms=machine.milliseconds() + 1000), "condition met")
        machine.run(until_ms=machine.milliseconds() + CABLE_STEADY_MS + 50)
        machine.host_absent = True
        machine.vbus = True
        connections = machine.usb_connect_count
        machine.run(until_ms=machine.milliseconds() + CHARGER_MS)
        self.assertFalse(host_seen())
        self.assertEqual(machine.usb_connect_count, connections + 1)
        self.assert_healthy(session)

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

    def test_on_the_charge_screen_connect_to_computer_in_the_prompt_starts_the_session(self):
        session, machine, asked, charge_frames, dark = self.charging_on_a_charger()
        machine.host_arrives()
        self.assertEqual(machine.run(until=lambda: asked,
                                     until_ms=machine.milliseconds() + PROMPT_NOTICED_MS),
                         "condition met")
        machine.run(until_ms=machine.milliseconds() + PROMPT_NOTICED_MS)
        self.assert_asked_without_enumerating(session, asked, 1)
        self.choose_connect(machine)
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertEqual(len(asked), 1)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.print_usb_log("charge screen, connect to computer", machine)


    def test_holding_back_in_dac_mode_still_returns_to_the_mode_selector(self):
        session, machine, _ = self.dac_session()
        self.hold_back(machine, LEAVE_HOLD_MS)
        machine.note("usb_dac_menu_open")
        machine.note("usb_modes")
        machine.run(until_ms=machine.milliseconds() + LEAVE_HOLD_MS + 1000)
        self.assertEqual(machine.usb_log[-1][1], "soft disconnect")
        self.assertNotIn("usb_storage_session", machine.reached)
        self.assertNotIn("usb_dac_menu_open", machine.reached)
        self.assertEqual(machine.reached.get("usb_modes"), 1)
        self.assert_healthy(session)

    def test_the_dac_menu_switches_to_connect_to_computer_without_unplugging(self):
        session, machine, _ = self.dac_session()
        for key in (KEY_MENU,) + (KEY_NEXT,) * DAC_MENU_COMPUTER_ROW + (KEY_PLAY,):
            self.press(machine, key)
        machine.answer_waits_for_key(4)
        self.assertEqual(machine.run(until=lambda: "usb_storage_session" in machine.reached,
                                     until_ms=machine.milliseconds() + 6000), "condition met")
        self.assert_healthy(session)

    def test_the_dac_level_meter_keeps_the_usb_audio_loop_fed(self):
        session, machine, path = self.dac_session()
        polls = self.poll_stamps(machine)
        areas = machine.trace("app_show_usb_area") if "app_show_usb_area" in machine.symbols else []
        state = self.stream_into_dac(machine)
        machine.run(until_ms=machine.milliseconds() + 500)
        self.assertIn("board_audio_stream_open", machine.reached)
        self.assertEqual(path.rate, DAC_RATE)
        start_areas, start_sent = len(areas), state["sent"]
        state["late_us"] = 0
        rate, gap = self.poll_rate(machine, DAC_MEASURE_MS, polls)
        sent = state["sent"] - start_sent
        frames = len(areas) - start_areas
        self.assertGreaterEqual(sent, DAC_MEASURE_MS - 5)
        self.assertLess(state["late_us"], 2000)
        self.assert_healthy(session)
        print("\n[dac stream] %d packets in %d ms, usb_poll %.0f/s, longest poll gap %.2f ms, "
              "latest packet %.2f ms behind, %d meter and info frames"
              % (sent, DAC_MEASURE_MS, rate, gap / 1000, state["late_us"] / 1000, frames))


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
