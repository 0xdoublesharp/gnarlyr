import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    IDENTITY,
    EchoImageHarness,
    main,
)

LUN_PRESENT_OFFSET = 17
OCR_WAIT_MS = 1000
BACKUP_BOOT_SECTOR = 6
STOCK_LABEL = "ECHO"


class EchoImageTest(EchoImageHarness):
    def label_sectors(self, session, volume):
        start = session.layout["volume_start"]
        return start + volume.cluster_lba(volume.root.first), start, start + BACKUP_BOOT_SECTOR

    def labels(self, session, volume):
        root, boot, backup = self.label_sectors(session, volume)
        return (session.disk.read_sector(root)[:11], session.disk.read_sector(boot)[71:82],
                session.disk.read_sector(backup)[71:82])

    def label_writes(self, session, volume):
        sectors = self.label_sectors(session, volume)
        return [(lba, count) for _, lba, count in session.card.writes
                if any(lba <= sector < lba + count for sector in sectors)]

    def test_a_stock_echo_label_is_renamed_once_at_start_and_stays_renamed(self):
        from echo_emulator.fat_volume import Fat32Volume
        volume = self.debug_volume(Fat32Volume(label=STOCK_LABEL))
        session = self.session(volume=volume)
        session.run_to_first_frame()
        renamed = IDENTITY.name.encode("ascii").ljust(11)
        self.assertEqual(self.labels(session, volume), (renamed,) * 3)
        self.assertTrue(self.label_writes(session, volume))
        again = session.reboot()
        again.run_to_first_frame()
        self.assertEqual(self.label_writes(again, volume), [])
        self.assertEqual(self.labels(again, volume), (renamed,) * 3)
        self.assert_healthy(again)

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
