import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    struct,
    CABLE_STEADY_MS,
    CHARGER_MS,
    KEY_MENU,
    KEY_NEXT,
    KEY_PLAY,
    PROMPT_NOTICED_MS,
)

USB_ANSWER_DAC = 3
SAFE_MILLIVOLTS = 3999
PIN_SETTLE_MS = 3000


class EchoImageTest(EchoImageHarness):
    def plugged_in_and_asked(self):
        session, machine, asked = self.asked_session()
        session.run_to_home_screen()
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: asked,
                                     until_ms=machine.milliseconds() + CABLE_STEADY_MS + 2000),
                         "condition met")
        machine.run(until_ms=machine.milliseconds() + PROMPT_NOTICED_MS)
        return session, machine, asked

    def steady_battery(self, machine):
        from unicorn.arm_const import UC_ARM_REG_R0

        def battery_gauge(machine):
            reading = [SAFE_MILLIVOLTS] * 8 + [0] * 56 + [8, 8]
            machine.uc.mem_write(machine.reg(UC_ARM_REG_R0), struct.pack("<66I", *reading))

        machine.stub_symbol("board_battery_gauge", battery_gauge)

    def pins_become(self, machine, pins):
        machine.run(until=lambda: self.usb_supply_pins(machine) == pins,
                    until_ms=machine.milliseconds() + PIN_SETTLE_MS)
        self.assertEqual(self.usb_supply_pins(machine), pins)

    def test_charge_in_the_prompt_keeps_the_player_charging_without_connecting(self):
        session, machine, asked = self.plugged_in_and_asked()
        for key in (KEY_NEXT, KEY_PLAY):
            self.press(machine, key)
        machine.run(until_ms=machine.milliseconds() + CHARGER_MS)
        self.assert_asked_without_enumerating(session, asked, 1)
        self.assertEqual(machine.usb_connect_count, 1)
        self.assert_healthy(session)

    def test_play_on_usb_power_in_the_prompt_lasts_until_the_cable_is_pulled(self):
        session, machine, asked = self.asked_session()
        self.steady_battery(machine)
        session.run_to_home_screen()
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: asked,
                                     until_ms=machine.milliseconds() + CABLE_STEADY_MS + 2000),
                         "condition met")
        machine.run(until_ms=machine.milliseconds() + PROMPT_NOTICED_MS)
        for key in (KEY_NEXT, KEY_NEXT, KEY_PLAY):
            self.press(machine, key)
        self.pins_become(machine, (0, 1))
        for name in ("usb_storage_session", "usb_dac_session"):
            self.assertNotIn(name, machine.reached)
        machine.vbus = False
        self.pins_become(machine, (0, 0))
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: len(asked) == 2,
                                     until_ms=machine.milliseconds() + CABLE_STEADY_MS + 2000),
                         "condition met")
        self.pins_become(machine, (1, 1))
        self.assert_healthy(session)

    def test_a_saved_usb_dac_answer_starts_the_dac_without_asking(self):
        session = self.session(usb_answer=USB_ANSWER_DAC)
        machine = session.machine
        machine.host_stays = True
        for name in ("usb_dac_session", "usb_storage_session"):
            machine.note(name)
        asked = machine.trace("app_ask_usb_mode")
        session.run_to_first_frame()
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: "usb_dac_session" in machine.reached,
                                     until_ms=machine.milliseconds() + 6000), "condition met")
        self.assertEqual(asked, [])
        self.assertNotIn("usb_storage_session", machine.reached)
        self.assert_healthy(session)

    def test_menu_on_the_charge_screen_opens_the_chooser_and_charge_returns(self):
        session, machine, asked, charge_frames, dark = self.charging_on_a_charger()
        machine.note("usb_modes")
        self.press(machine, KEY_MENU)
        self.assertEqual(machine.reached.get("usb_modes"), 1)
        self.press(machine, KEY_PLAY)
        drawn = len(charge_frames)
        machine.run(until=lambda: len(charge_frames) > drawn,
                    until_ms=machine.milliseconds() + CHARGER_MS)
        self.assertGreater(len(charge_frames), drawn)
        self.assertEqual(asked, [])
        for name in ("usb_storage_session", "usb_dac_session"):
            self.assertNotIn(name, machine.reached)
        self.assertEqual(self.usb_supply_pins(machine), (1, 1))
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
