import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
)

LUN_PRESENT_OFFSET = 17
OCR_WAIT_MS = 1000


class EchoImageTest(EchoImageHarness):
    def test_a_lost_emmc_start_flag_is_noticed_and_the_internal_drive_is_offered_again(self):
        from unicorn.arm_const import UC_ARM_REG_R1
        script = ("build %s\nwait 100\n" % self.build_tag()).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        offered = []
        machine.hook_symbol("msc_init", lambda: offered.append(machine.read_memory(
            machine.reg(UC_ARM_REG_R1) + LUN_PRESENT_OFFSET, 1)[0]))
        started = machine.symbols["started"]
        machine.hook_symbol("hal_system_usb_reconnect",
                            lambda: machine.uc.mem_write(started, b"\x00"))
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(machine.read_memory(started, 1), b"\x00")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        self.assertEqual(offered, [1, 1])
        report = self.read_report(session)
        steps = [event["detail"].split()[0] for event in report["events"]
                 if event["kind"] == "scan" and event["detail"].startswith("emmc")]
        print("\n[internal drive] eMMC events: %s" % steps)
        self.assertEqual(steps[-2:], ["emmc_flag_lost", "emmc_ok"])
        self.assert_healthy(session)

    def test_an_emmc_that_needs_a_second_reset_is_retried_before_the_drives_are_offered(self):
        from unicorn.arm_const import UC_ARM_REG_R1
        script = ("build %s\nwait 100\n" % self.build_tag()).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        offered = []
        machine.hook_symbol("msc_init", lambda: offered.append(machine.read_memory(
            machine.reg(UC_ARM_REG_R1) + LUN_PRESENT_OFFSET, 1)[0]))
        started = machine.symbols["started"]
        resets = []
        answer = session.card.command

        def stuck_answer(index, argument):
            if lost and index == 0:
                resets.append(machine.milliseconds())
            if lost and index == 1 and len(resets) < 2:
                return (0, 0, 0, 0)
            return answer(index, argument)

        lost = []

        def lose_flag():
            machine.uc.mem_write(started, b"\x00")
            lost.append(True)

        session.card.command = stuck_answer
        machine.hook_symbol("hal_system_usb_reconnect", lose_flag)
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        self.assertEqual(offered, [1, 1])
        report = self.read_report(session)
        starts = [event["detail"].split() for event in report["events"]
                  if event["kind"] == "scan" and event["detail"].startswith("emmc")]
        print("\n[internal drive] stuck eMMC events: %s, resets %s" % (starts, resets))
        self.assertEqual(starts[-1][0], "emmc_ok")
        self.assertGreaterEqual(int(starts[-1][1]), OCR_WAIT_MS)
        self.assertEqual(len(resets), 2)
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
