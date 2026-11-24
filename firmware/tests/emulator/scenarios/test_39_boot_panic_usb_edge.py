import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    GPIO_DATA,
    IDENTITY,
    KEY_PLAY,
    main,
    pin_high_index,
)
from echo_emulator.audio import AudioPath
from echo_emulator.bluetooth import Headphones
from echo_emulator.emmc import SdCard, SparseDisk
from echo_emulator.fat_volume import Fat32Volume
from echo_emulator.playback import ALBUM_COUNT, Rig
from echo_emulator.real_volume import real_volume
from echo_emulator.usb_storage_host import ScsiHost, StorageHost
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_MSP, UC_ARM_REG_PC, UC_ARM_REG_R0
from usb_audio_host import UsbAudioHost

BUDS_ADDRESS = bytes([0x5C, 0x17, 0xCF, 1, 2, 3])
BUDS = "Buds"
DEVICE_BYTES = 6 + 32
DEVICE_COUNT = 6
STORE_BYTES = 4096
USB_ANSWER_DAC = 3
CARD_LUN = 1
SECTOR = 512
CARD_TYPE_52_MHZ = 0x03
CRU_MMC_DIVIDERS = 0x30
SD_FIELD = 0x00FF
SD_PLL_QUARTER = 0x0003


def bluetooth_store(on_bluetooth):
    device = BUDS_ADDRESS + BUDS.encode("ascii").ljust(32, b"\0")
    record = b"BT\x01\x01\x01" + device + bytes(DEVICE_BYTES * (DEVICE_COUNT - 1))
    record += b"\x01" if on_bluetooth else b"\x00"
    return record.ljust(STORE_BYTES, b"\0")


class EchoImageTest(EchoImageHarness):
    def sleep_screen(self, session):
        machine = session.machine
        now = machine.milliseconds()
        machine.hold_power(now + 20, 150)
        asleep = machine.symbols["panel_asleep"]
        reason = machine.run(until=lambda: machine.read_memory(asleep, 1)[0] == 1,
                             until_ms=now + 4000)
        self.assertEqual(reason, "condition met")

    def test_low_battery_boot_renders_notice_before_power_off(self):
        session = self.session()
        machine = session.machine
        machine.adc_battery_value = 600
        machine.note("app_start")
        reason = machine.run(until_ms=5000)
        self.assertEqual(reason, "firmware reached hal_power_off")
        self.assertNotIn("app_start", machine.reached)
        self.assertGreater(session.panel.painted_pixels(), 0)
        self.assertGreater(session.panel.colors(), 1)
        self.assertIsNotNone(pin_high_index(machine, 2, 4))
        self.assertGreaterEqual(machine.milliseconds(), 2000)
        self.assertEqual(session.changed_sectors(), [])
        self.assertEqual(machine.faults, [])

    def test_sd_eject_notice_survives_past_stall_timeout_and_sleeps_in_wfi(self):
        slot = self.with_card()
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=Fat32Volume(), module_elfs=self.module_elfs, prepare=slot.insert)
        machine = rig.machine
        notices = machine.collect_notices()
        waiting = [False]
        wakes = [0]

        def on_wait():
            waiting[0] = True

        def on_profile_wait():
            if waiting[0]:
                wakes[0] += 1

        machine.hook_symbol("board_wait_for_key", on_wait)
        machine.hook_symbol("board_profile_wait", on_profile_wait)
        rig.press("previous")
        rig.press("play")
        rig.press_many("previous", 2)
        rig.press("play")
        rig.press_many("previous", 2)
        rig.press("play")
        self.assertTrue(waiting[0])
        self.assertIn(("Eject card", "Safe to remove card"), notices)
        machine.run(until_ms=machine.milliseconds() + 12500)
        self.assertEqual(machine.panics, [])
        self.assertGreater(wakes[0], 100)
        waiting[0] = False
        rig.press("play")
        rig.run_for(500)
        self.assert_healthy(rig.session)

    def test_panic_stall_wakes_asleep_panel_without_deadlock(self):
        session = self.session()
        machine = session.machine
        session.run_to_home_screen()
        self.sleep_screen(session)
        commands_before = len(session.panel.groups)
        windows_before = len(session.panel.windows)
        stall_addr = machine.symbols["board_profile_wait"] | 1
        machine.hook_symbol("board_profile_wait",
                            lambda: machine.uc.reg_write(UC_ARM_REG_LR, stall_addr))
        reason = machine.run(until_ms=machine.milliseconds() + 13000)
        self.assertEqual(reason, "firmware reached board_panic_stall")
        returned = [False]
        lr = machine.reg(UC_ARM_REG_LR) & ~1
        machine.add_address_hook(lr, lambda: returned.__setitem__(0, True))
        machine.continue_past_stop()
        reason = machine.run(until=lambda: returned[0], until_ms=machine.milliseconds() + 1500)
        self.assertEqual(reason, "condition met")
        commands = [group[0] for group in session.panel.groups[commands_before:]]
        self.assertIn(0x11, commands)
        self.assertIn(0x29, commands)
        self.assertEqual(machine.gpio[2].regs[GPIO_DATA] >> 4 & 1, 1)
        drawn = sum(w.nonzero for w in session.panel.windows[windows_before:])
        self.assertGreater(drawn, 0)
        self.assertEqual(machine.faults, [])

    def test_panic_fault_wakes_asleep_panel_and_honors_power_hold(self):
        session = self.session()
        machine = session.machine
        session.run_to_home_screen()
        self.sleep_screen(session)
        commands_before = len(session.panel.groups)
        windows_before = len(session.panel.windows)
        sp = machine.reg(UC_ARM_REG_MSP) - 32
        frame = struct.pack("<8I", 1, 2, 3, 4, 5, 0x03051235, 0x03052344, 0x01000000)
        machine.uc.mem_write(sp, frame)
        machine.uc.reg_write(UC_ARM_REG_MSP, sp)
        machine.uc.reg_write(UC_ARM_REG_PC, machine.symbols["device_fault_entry"])
        machine.exception_depth = 1
        reason = machine.run(until_ms=machine.milliseconds() + 1000)
        self.assertEqual(reason, "firmware reached device_fault_entry")
        machine.continue_past_stop()
        reason = machine.run(until_ms=machine.milliseconds() + 1000)
        self.assertEqual(reason, "firmware reached board_panic_fault")
        returned = [False]
        lr = machine.reg(UC_ARM_REG_LR) & ~1
        machine.add_address_hook(lr, lambda: returned.__setitem__(0, True))
        machine.continue_past_stop()
        reason = machine.run(until=lambda: returned[0], until_ms=machine.milliseconds() + 1500)
        self.assertEqual(reason, "condition met")
        commands = [group[0] for group in session.panel.groups[commands_before:]]
        self.assertIn(0x11, commands)
        self.assertIn(0x29, commands)
        self.assertEqual(machine.gpio[2].regs[GPIO_DATA] >> 4 & 1, 1)
        drawn = sum(w.nonzero for w in session.panel.windows[windows_before:])
        self.assertGreater(drawn, 0)
        now = machine.milliseconds()
        machine.hold_power(now, 4000)
        reason = machine.run(until_ms=now + 4000)
        self.assertEqual(reason, "firmware reached board_power_cut")
        self.assertEqual(machine.faults, [])

    def test_usb_dac_unroutes_active_bluetooth_before_streaming(self):
        volume = real_volume(playable=True, album_count=ALBUM_COUNT)
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("bluetooth.bin", bluetooth_store(True), None, IDENTITY.data_directory)

        def prepare(machine):
            machine.bluetooth.headphones = Headphones(BUDS_ADDRESS, BUDS)
            machine.usb_answer = USB_ANSWER_DAC

        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume,
                  module_elfs=self.module_elfs, prepare=prepare)
        machine = rig.machine
        module = machine.bluetooth
        rig.open_songs()
        rig.play_song(1)
        reason = machine.run(until=lambda: module.streaming, until_ms=machine.milliseconds() + 10000)
        self.assertEqual(reason, "condition met")
        routed_addr = machine.symbols["routed"]
        powered_addr = machine.symbols["powered"]
        self.assertEqual(machine.read_memory(routed_addr, 1)[0], 1)
        self.assertEqual(machine.read_memory(powered_addr, 1)[0], 1)
        machine.host_stays = True
        audio = rig.audio
        machine.note("usb_dac_session")
        host = UsbAudioHost(machine, 48000, 4, True)
        host.enumerate()
        machine.vbus = True
        reason = machine.run(
            until=lambda: "usb_dac_session" in machine.reached and audio.running and audio.rate == 48000,
            until_ms=machine.milliseconds() + 6000)
        self.assertEqual(reason, "condition met")
        self.assertEqual(machine.read_memory(routed_addr, 1)[0], 0)
        self.assertEqual(machine.read_memory(powered_addr, 1)[0], 0)
        self.assertEqual(audio.rate, 48000)
        self.assert_healthy(rig.session)

    def test_usb_sd_eject_clears_fast_flag_before_speed_down(self):
        session = self.session(vbus=True)
        machine = session.machine
        disk = SparseDisk(1024 * 1024)
        Fat32Volume(fsinfo=True).write_to(disk, 0)
        card = SdCard(disk, machine)
        card.high_speed = True
        session.card.card_type = CARD_TYPE_52_MHZ
        machine.card_present = True
        machine.sd_controller.card = card
        machine.host_stays = True
        machine.stub_symbol("msc_finished", lambda m: 0)
        machine.stub_symbol("usb_activity_quiet", lambda m: 1)
        host = StorageHost(machine, SECTOR, 40)
        machine.usb.host = host
        machine.note("usb_activity_start")
        session.run_to_first_frame()
        self.assertEqual(machine.run(until=lambda: machine.usb_reset_ack,
                                     until_ms=self.session_module.FRAME_LIMIT_MS), "condition met")
        host.enumerate()
        self.assertEqual(machine.run(until=host.configured,
                                     until_ms=machine.milliseconds() + 2000), "condition met")
        self.assertEqual(machine.run(until=lambda: "usb_activity_start" in machine.reached,
                                     until_ms=self.session_module.FRAME_LIMIT_MS), "condition met")
        machine.run(until_ms=machine.milliseconds() + 300)
        scsi = ScsiHost(machine, host)
        self.assertTrue(scsi.until_ready(CARD_LUN))
        self.assertEqual(machine.cru.regs.get(CRU_MMC_DIVIDERS, 0) & SD_FIELD, SD_PLL_QUARTER)
        cleared = [False]
        machine.hook_symbol("usb_card_clear_fast", lambda: cleared.__setitem__(0, True))
        machine.card_present = False
        reason = machine.run(until=lambda: cleared[0], until_ms=machine.milliseconds() + 2000)
        self.assertEqual(reason, "condition met")
        machine.run(until_ms=machine.milliseconds() + 200)
        self.assertFalse(scsi.until_ready(CARD_LUN, tries=3))
        in_speed_down = [False]
        sd_clocks_on_exit = []
        sd_host_addr = machine.symbols["SD_MMC_HOST"]
        machine.hook_symbol("usb_card_speed_down", lambda: in_speed_down.__setitem__(0, True))

        def on_set_clock():
            if in_speed_down[0] and machine.reg(UC_ARM_REG_R0) == sd_host_addr:
                sd_clocks_on_exit.append(machine.milliseconds())

        machine.hook_symbol("mmc_set_clock", on_set_clock)
        machine.answer_waits_for_key(2)
        machine.host_stays = False
        machine.vbus = False
        machine.run(until=lambda: in_speed_down[0], until_ms=machine.milliseconds() + 3000)
        self.assertTrue(in_speed_down[0])
        machine.run(until_ms=machine.milliseconds() + 200)
        self.assertEqual(sd_clocks_on_exit, [])
        self.assert_healthy(session)

    def test_usb_audio_stalled_deferred_setup_drops_control_data(self):
        session, machine, asked = self.asked_session()
        machine.host_stays = True
        audio = AudioPath(machine)
        host = UsbAudioHost(machine, 48000, 4, True)
        host.enumerate()
        receives = []
        machine.hook_symbol("usb_audio_receive", lambda: receives.append(machine.milliseconds()))
        session.run_to_home_screen()
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: asked,
                                     until_ms=machine.milliseconds() + 3000), "condition met")
        for key in (100, 100, 100, KEY_PLAY):
            self.press(machine, key)
        self.assertEqual(machine.run(until=lambda: audio.running,
                                     until_ms=machine.milliseconds() + 3000), "condition met")
        before = len(receives)
        host.setup(0x21, 0x99, 0x0100, 0x0200, 2, b"\x11\x22")
        machine.run(until_ms=machine.milliseconds() + 400)
        self.assertEqual(len(receives), before)
        self.assertEqual(audio.output_underruns(), 0)
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
