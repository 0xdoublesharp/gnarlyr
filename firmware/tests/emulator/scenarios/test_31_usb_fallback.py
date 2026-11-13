import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, CABLE_STEADY_MS

DCFG = 0x800
SPEED_MASK = 3
HIGH_SPEED = 0
FULL_SPEED = 1
RESET_COUNT = 6
RESET_GAP_MS = 100
SERVING_LIMIT_MS = 6000
SETTLE_MS = 1500


class EchoImageTest(EchoImageHarness):
    def serving(self, machine, sessions):
        return machine.run(
            until=lambda: machine.reached.get("usb_activity_start", 0) >= sessions,
            until_ms=machine.milliseconds() + SERVING_LIMIT_MS)

    def test_a_reset_only_host_falls_back_and_the_next_plug_retries_high_speed(self):
        session = self.session()
        machine = session.machine
        machine.host_stays = True
        machine.stub_symbol("msc_finished", lambda machine: 0)
        machine.note("usb_activity_start")
        machine.note("usb_session_storage_changed")
        machine.answer_waits_for_key(8)
        speeds = []
        connected = machine.usb_connected

        def record_speed():
            speeds.append(machine.usb.regs.get(DCFG, 0) & SPEED_MASK)
            connected()

        machine.usb_connected = record_speed
        session.run_to_first_frame()
        machine.vbus = True
        self.assertEqual(self.serving(machine, 1), "condition met")
        start = machine.milliseconds()
        for index in range(RESET_COUNT):
            machine.at_ms(start + (index + 1) * RESET_GAP_MS, machine.usb.raise_reset)
        machine.run(until_ms=start + (RESET_COUNT + 1) * RESET_GAP_MS + SETTLE_MS)
        self.assertEqual(speeds[-2:], [HIGH_SPEED, FULL_SPEED])
        machine.vbus = False
        self.assertEqual(machine.run(
            until=lambda: machine.reached.get("usb_session_storage_changed", 0) >= 1,
            until_ms=machine.milliseconds() + SERVING_LIMIT_MS), "condition met")
        machine.run(until_ms=machine.milliseconds() + CABLE_STEADY_MS + SETTLE_MS)
        machine.vbus = True
        self.assertEqual(self.serving(machine, 2), "condition met")
        self.assertEqual(speeds[-1], HIGH_SPEED)
        values = self.read_report(session)["values"]
        self.assertTrue(values["usb.speed_fallback"].startswith("resets commands=0"), values)
        self.assert_healthy(session)
        self.print_usb_log("reset-only fallback and next-plug high-speed retry", machine)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
