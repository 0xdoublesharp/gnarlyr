import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main

CARD_LUN = 1
SECTOR = 512
COMMAND_SECTORS = 256
COMMANDS = int(os.environ.get("ECHO_THROUGHPUT_COMMANDS", "4"))
FIRST_LBA = 8192
SETTLE_MS = 300
CONFIGURE_LIMIT_MS = 2000
INT_DATA_CRC = 1 << 7
FAILING_WORD = 3000


class EchoImageTest(EchoImageHarness):
    def storage_host(self):
        from echo_emulator.emmc import SdCard, SparseDisk
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.usb_storage_host import ScsiHost, StorageHost
        session = self.session(vbus=True)
        machine = session.machine
        disk = SparseDisk(1024 * 1024)
        Fat32Volume(fsinfo=True).write_to(disk, 0)
        card = SdCard(disk, machine)
        machine.card_present = True
        machine.sd_controller.card = card
        machine.host_stays = True
        machine.stub_symbol("msc_finished", lambda machine: 0)
        machine.stub_symbol("usb_activity_quiet", lambda machine: 1)
        host = StorageHost(machine)
        machine.usb.host = host
        machine.note("usb_activity_start")
        session.run_to_first_frame()
        self.assertEqual(machine.run(until=lambda: machine.usb_reset_ack,
                                     until_ms=self.session_module.FRAME_LIMIT_MS), "condition met")
        host.enumerate()
        self.assertEqual(machine.run(until=host.configured,
                                     until_ms=machine.milliseconds() + CONFIGURE_LIMIT_MS),
                         "condition met")
        self.assertEqual(machine.run(until=lambda: "usb_activity_start" in machine.reached,
                                     until_ms=self.session_module.FRAME_LIMIT_MS), "condition met")
        machine.run(until_ms=machine.milliseconds() + SETTLE_MS)
        scsi = ScsiHost(machine, host)
        self.assertTrue(scsi.until_ready(CARD_LUN))
        return session, machine, card, disk, host, scsi

    def pattern(self, index):
        return bytes((index * 7 + offset * 13) & 0xFF for offset in range(COMMAND_SECTORS * SECTOR))

    def test_card_writes_stream_one_command_each_and_read_back_intact(self):
        session, machine, card, disk, host, scsi = self.storage_host()
        written = [self.pattern(index) for index in range(COMMANDS)]
        writes_before = len(card.writes)
        start = machine.microseconds()
        for index, data in enumerate(written):
            lba = FIRST_LBA + index * COMMAND_SECTORS
            self.assertEqual(scsi.write(CARD_LUN, lba, data), (0, 0))
        write_us = machine.microseconds() - start
        card_writes = card.writes[writes_before:]
        start = machine.microseconds()
        for index, data in enumerate(written):
            lba = FIRST_LBA + index * COMMAND_SECTORS
            read, status = scsi.read(CARD_LUN, lba, COMMAND_SECTORS)
            self.assertEqual(status, (0, 0))
            self.assertEqual(read, data)
        read_us = machine.microseconds() - start
        total = COMMANDS * COMMAND_SECTORS * SECTOR
        for index, data in enumerate(written):
            lba = FIRST_LBA + index * COMMAND_SECTORS
            self.assertEqual(disk.read(lba, COMMAND_SECTORS), data)
        print("\n[usb throughput] card write %.2f MB/s, read %.2f MB/s over %d KiB; "
              "%d card write commands for %d WRITE(10)s, OUT armed for %s packets"
              % (total / write_us, total / read_us, total // 1024, len(card_writes), COMMANDS,
                 sorted(set(host.armed_packets))))
        self.assertEqual([count for _, _, count in card_writes], [COMMAND_SECTORS] * COMMANDS)
        self.assert_healthy(session)

    def test_a_card_write_error_reaches_the_host_as_a_check_condition(self):
        session, machine, card, disk, host, scsi = self.storage_host()
        controller = machine.sd_controller
        accept = controller.accept_word
        state = {"words": 0}

        def failing_word(value):
            accept(value)
            state["words"] += 1
            if state["words"] == FAILING_WORD:
                controller.raise_status(INT_DATA_CRC)

        controller.accept_word = failing_word
        status, residue = scsi.write(CARD_LUN, FIRST_LBA, self.pattern(0))
        self.assertEqual((status, residue), (1, COMMAND_SECTORS * SECTOR))
        controller.accept_word = accept
        sense, status = scsi.request_sense(CARD_LUN)
        self.assertEqual(status, (0, 0))
        self.assertEqual((sense[2], sense[12]), (0x03, 0x0C))
        self.assertEqual(scsi.write(CARD_LUN, FIRST_LBA, self.pattern(1)), (0, 0))
        self.assertEqual(disk.read(FIRST_LBA, COMMAND_SECTORS), self.pattern(1))
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
