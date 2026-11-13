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
MMC_FIFO = 0x200
FAILING_WORD = 3000
CRU_MMC_DIVIDERS = 0x30
SD_FIELD = 0x00FF
EMMC_FIELD = 0xFF00
SD_CRYSTAL = 0x0080
SD_PLL_QUARTER = 0x0003
EMMC_CRYSTAL = 0x8000
EMMC_PLL_QUARTER = 0x0300
CARD_TYPE_52_MHZ = 0x03
SWITCH_HIGH_SPEED_TIMING = 0x03B90100
LEAVE_LIMIT_MS = 5000


class EchoImageTest(EchoImageHarness):
    def storage_host(self, fast=False):
        from echo_emulator.emmc import SdCard, SparseDisk
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.usb_storage_host import ScsiHost, StorageHost
        session = self.session(vbus=True)
        machine = session.machine
        disk = SparseDisk(1024 * 1024)
        Fat32Volume(fsinfo=True).write_to(disk, 0)
        card = SdCard(disk, machine)
        card.high_speed = fast
        session.card.card_type = CARD_TYPE_52_MHZ if fast else 0
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

    def clock_fields(self, machine):
        value = machine.cru.regs.get(CRU_MMC_DIVIDERS, 0)
        return value & SD_FIELD, value & EMMC_FIELD

    def measure(self, fast):
        session, machine, card, disk, host, scsi = self.storage_host(fast)
        written = [self.pattern(index) for index in range(COMMANDS)]
        writes_before = len(card.writes)
        start = machine.microseconds()
        for index, data in enumerate(written):
            lba = FIRST_LBA + index * COMMAND_SECTORS
            self.assertEqual(scsi.write(CARD_LUN, lba, data), (0, 0))
        write_us = machine.microseconds() - start
        card_writes = card.writes[writes_before:]
        reads_before = len(card.reads)
        start = machine.microseconds()
        for index, data in enumerate(written):
            lba = FIRST_LBA + index * COMMAND_SECTORS
            read, status = scsi.read(CARD_LUN, lba, COMMAND_SECTORS)
            self.assertEqual(status, (0, 0))
            self.assertEqual(read, data)
        read_us = machine.microseconds() - start
        card_reads = [count for _, _, count in card.reads[reads_before:]]
        total = COMMANDS * COMMAND_SECTORS * SECTOR
        for index, data in enumerate(written):
            lba = FIRST_LBA + index * COMMAND_SECTORS
            self.assertEqual(disk.read(lba, COMMAND_SECTORS), data)
        print("\n[usb throughput%s] card write %.2f MB/s, read %.2f MB/s over %d KiB; "
              "%d card write commands for %d WRITE(10)s, OUT armed for %s packets"
              % (" at 50 MHz" if fast else "", total / write_us, total / read_us, total // 1024,
                 len(card_writes), COMMANDS, sorted(set(host.armed_packets))))
        self.assertEqual([count for _, _, count in card_writes], [COMMAND_SECTORS] * COMMANDS)
        self.assertEqual(card_reads, [COMMAND_SECTORS] * COMMANDS)
        self.assert_healthy(session)
        return session, machine, card

    def test_card_writes_stream_one_command_each_and_read_back_intact(self):
        session, machine, card = self.measure(fast=False)
        self.assertEqual(self.clock_fields(machine), (SD_CRYSTAL, EMMC_CRYSTAL))
        self.assertNotIn(SWITCH_HIGH_SPEED_TIMING,
                         [argument for _, index, argument in session.card.commands if index == 6])

    def test_both_cards_run_at_50_mhz_during_the_session_and_return_to_the_crystal(self):
        session, machine, card = self.measure(fast=True)
        self.assertEqual(self.clock_fields(machine), (SD_PLL_QUARTER, EMMC_PLL_QUARTER))
        self.assertEqual(card.switches, [0x00FFFFF1, 0x80FFFFF1])
        self.assertIn(SWITCH_HIGH_SPEED_TIMING,
                      [argument for _, index, argument in session.card.commands if index == 6])
        machine.note("usb_session_stop_listening")
        machine.host_stays = False
        machine.vbus = False
        self.assertEqual(machine.run(until=lambda: "usb_session_stop_listening" in machine.reached,
                                     until_ms=machine.milliseconds() + LEAVE_LIMIT_MS),
                         "condition met")
        self.assertEqual(self.clock_fields(machine), (SD_CRYSTAL, EMMC_CRYSTAL))

    def test_a_card_write_error_reaches_the_host_and_drops_the_card_to_the_crystal(self):
        session, machine, card, disk, host, scsi = self.storage_host(fast=True)
        self.assertEqual(self.clock_fields(machine)[0], SD_PLL_QUARTER)
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
        self.assertEqual(self.clock_fields(machine), (SD_CRYSTAL, EMMC_PLL_QUARTER))
        self.assertEqual(scsi.write(CARD_LUN, FIRST_LBA, self.pattern(1)), (0, 0))
        self.assertEqual(disk.read(FIRST_LBA, COMMAND_SECTORS), self.pattern(1))
        self.assert_healthy(session)

    def test_a_card_read_error_at_speed_fails_that_read_and_the_repeat_reads_at_the_crystal(self):
        session, machine, card, disk, host, scsi = self.storage_host(fast=True)
        data = self.pattern(2)
        self.assertEqual(scsi.write(CARD_LUN, FIRST_LBA, data), (0, 0))
        controller = machine.sd_controller
        read = controller.read
        state = {"words": 0}

        def failing_read(offset, size):
            value = read(offset, size)
            if offset == MMC_FIFO:
                state["words"] += 1
                if state["words"] == FAILING_WORD:
                    controller.raise_status(INT_DATA_CRC)
            return value

        controller.read = failing_read
        _, status = scsi.read(CARD_LUN, FIRST_LBA, COMMAND_SECTORS)
        controller.read = read
        self.assertEqual(status, (1, COMMAND_SECTORS * SECTOR))
        sense, _ = scsi.request_sense(CARD_LUN)
        self.assertEqual((sense[2], sense[12]), (0x03, 0x11))
        self.assertEqual(self.clock_fields(machine)[0], SD_CRYSTAL)
        read, status = scsi.read(CARD_LUN, FIRST_LBA, COMMAND_SECTORS)
        self.assertEqual((read, status), (data, (0, 0)))
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
