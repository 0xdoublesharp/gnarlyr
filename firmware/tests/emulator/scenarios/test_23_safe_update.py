import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    CHUNK_SECTORS,
    IDENTITY,
    LOADER_FLAG_REGISTER,
    LOADER_FLAG_VALUE,
    SLOT_SECTORS,
    TRIAL_ROLLED_BACK,
    TRIAL_RUNNING,
    english,
    struct,
    summary,
)
from echo_emulator.machine import VOLUME_UP_KEY_ADC
import make_image
import prove_upgrade

HANG = b"\xfe\xe7"
LUN_PRESENT_OFFSET = 17
TRIAL_BOOT_MODULE = 15
SETTLE_MS = 20000
RESET_HOLD_MS = 10000
SCAN_MS = 5
LIMIT_MS = 40000


class EchoImageTest(EchoImageHarness):
    def slot_bytes(self, image):
        return image[:self.session_module.SLOT_BYTES]

    def firmware_writes(self, session, slot):
        start = session.layout[slot]
        return [(lba, count, word) for kind, lba, count, *rest in session.card.accesses
                if kind == "W" and start <= lba < start + SLOT_SECTORS for word in rest]

    def assert_written_header_last(self, session, slot, image):
        sequence = self.firmware_writes(session, slot)
        start = session.layout[slot]
        self.assertEqual(sequence[0], (start, CHUNK_SECTORS, 0xFFFFFFFF))
        self.assertEqual(sequence[-1], (start, CHUNK_SECTORS, struct.unpack_from("<I", image)[0]))
        self.assertNotIn(start, [lba for lba, _, _ in sequence[1:-1]])

    def install_on_trial(self, update):
        session = self.session(files=[(IDENTITY.update_file, update, None)], vbus=True)
        session.machine.note("updater_install")
        self.assertEqual(self.boot_until_waiting_for_key(session), "reached board_wait_for_power_hold")
        self.assertEqual(session.machine.reached["trial_module_install"], 1)
        self.assertNotIn("updater_install", session.machine.reached)
        self.assert_healthy(session)
        return session

    def start(self, previous, image, vbus=False):
        session = previous.reboot(image=image, vbus_after_first_frame=vbus)
        session.machine.note("trial_boot_module_start")
        return session

    def test_an_update_goes_into_slot_1_on_trial_and_slot_2_keeps_the_running_build(self):
        newer = self.newer_image()
        session = self.install_on_trial(newer)
        self.assertEqual(self.firmware_writes(session, "slot_2"), [])
        self.assert_written_header_last(session, "slot_1", newer)
        self.assert_every_write_read_back(
            [item for item in session.card.accesses
             if item[0] != "W" or item[1] < session.layout["volume_start"]])
        self.assertEqual(session.slot_image("slot_1"), self.slot_bytes(newer))
        self.assertEqual(session.slot_image("slot_2"), self.slot_bytes(self.image))
        self.assertEqual(self.trial_record(session), (TRIAL_RUNNING, 0))
        self.assertFalse(session.file_exists(IDENTITY.update_file))
        summary("trial install", session)

    def test_a_trial_build_confirms_with_an_ejected_session_and_becomes_the_backup(self):
        newer = self.newer_image()
        trial = self.start(self.install_on_trial(newer), newer, vbus=True)
        machine = trial.machine
        machine.host_ejects()
        notices = machine.collect_notices()
        machine.note("usb_storage_session")
        self.assertEqual(trial.run_to_first_frame(), "condition met")
        self.assertEqual(self.trial_record(trial), (TRIAL_RUNNING, 1))
        self.assertEqual(trial.run_through_update_check(), "reached usb_session_storage_changed")
        self.assertTrue(prove_upgrade.run_until_promoted(trial))
        self.assert_healthy(trial)
        self.assertIn(english("STR_UPDATE_BACKUP"), [title for title, _ in notices])
        self.assertEqual(self.firmware_writes(trial, "slot_1"), [])
        self.assert_written_header_last(trial, "slot_2", newer)
        self.assertEqual(trial.slot_image("slot_1"), self.slot_bytes(newer))
        self.assertEqual(trial.slot_image("slot_2"), self.slot_bytes(newer))
        again = self.start(trial, newer)
        self.assertEqual(again.run_to_first_frame(), "condition met")
        self.assertNotIn("trial_boot_module_start", again.machine.reached)
        self.assertEqual(again.written_ranges(), [])
        summary("trial confirmed", trial)

    def test_a_trial_session_offers_the_drives_without_loading_a_module_behind_the_app(self):
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1
        newer = self.newer_image()
        code_end = make_image.OVERLAY_BASE + struct.unpack_from(
            "<I", newer, make_image.USB_OVERLAY_OFFSET + 4)[0]
        self.assertGreater(code_end, make_image.MODULE_BASE)
        trial = self.start(self.install_on_trial(newer), newer, vbus=True)
        machine = trial.machine
        loads = []
        offered = []
        machine.hook_symbol("hal_system_load_module",
                            lambda: loads.append((machine.reg(UC_ARM_REG_R0),
                                                  "usb_storage_session" in machine.reached)))
        machine.hook_symbol("msc_init", lambda: offered.append(machine.read_memory(
            machine.reg(UC_ARM_REG_R1) + LUN_PRESENT_OFFSET, 1)[0]))
        machine.note("usb_storage_session")
        self.assertEqual(trial.run_to_first_frame(), "condition met")
        self.assertEqual(trial.run_through_update_check(), "reached usb_session_storage_changed")
        self.assert_healthy(trial)
        self.assertEqual(offered, [1])
        self.assertEqual([module for module, _ in loads].count(TRIAL_BOOT_MODULE), 1)
        self.assertNotIn((TRIAL_BOOT_MODULE, True), loads)
        self.assertEqual(self.trial_record(trial), (TRIAL_RUNNING, 2))

    def test_a_trial_session_that_ends_unplugged_does_not_confirm(self):
        newer = self.newer_image()
        trial = self.start(self.install_on_trial(newer), newer, vbus=True)
        self.assertEqual(trial.run_to_first_frame(), "condition met")
        self.assertEqual(trial.run_through_update_check(), "reached usb_session_storage_changed")
        self.assertEqual(self.trial_record(trial), (TRIAL_RUNNING, 2))
        self.assertEqual(trial.slot_image("slot_2"), self.slot_bytes(self.image))

    def test_a_start_that_runs_twenty_seconds_is_not_counted(self):
        newer = self.newer_image()
        trial = self.start(self.install_on_trial(newer), newer)
        self.assertEqual(trial.run_to_first_frame(), "condition met")
        self.assertEqual(self.trial_record(trial), (TRIAL_RUNNING, 1))
        trial.machine.run(until_ms=SETTLE_MS + 1000)
        self.assert_healthy(trial)
        self.assertEqual(self.trial_record(trial), (TRIAL_RUNNING, 0))

    def test_a_trial_build_that_never_settles_is_rolled_back_at_the_fourth_start(self):
        newer = self.newer_image()
        session = self.install_on_trial(newer)
        for attempt in (1, 2, 3):
            session = self.start(session, newer)
            self.assertEqual(session.run_to_first_frame(), "condition met")
            self.assertEqual(self.trial_record(session), (TRIAL_RUNNING, attempt))
        rolling = self.start(session, newer)
        self.assertEqual(rolling.run_until_reset(), "system reset requested")
        self.assert_healthy(rolling)
        self.assertEqual(rolling.machine.reached["trial_boot_module_start"], 1)
        self.assertEqual(self.firmware_writes(rolling, "slot_2"), [])
        self.assert_written_header_last(rolling, "slot_1", self.image)
        self.assertEqual(rolling.slot_image("slot_1"), self.slot_bytes(self.image))
        self.assertEqual(rolling.slot_image("slot_2"), self.slot_bytes(self.image))
        self.assertEqual(self.trial_record(rolling), (TRIAL_ROLLED_BACK, 0))
        self.assertEqual(rolling.panel.lit_sweeps(), [])
        self.assert_rollback_reported(rolling)

    def assert_rollback_reported(self, previous):
        restored = self.start(previous, previous.slot_image("slot_1") + self.image[-4:])
        notices = restored.machine.collect_notices()
        self.assertEqual(restored.run_until_waiting_for_key(presses=0),
                         "reached board_wait_for_key")
        self.assertEqual(notices[-1], (english("STR_UPDATE_ROLLED_BACK"),
                                       english("STR_UPDATE_ROLLED_BACK_HINT")))
        self.assertIsNone(self.trial_record(restored))
        self.assert_healthy(restored)
        restored_again = self.start(restored, self.image)
        self.assertEqual(restored_again.run_to_first_frame(), "condition met")
        self.assertNotIn("trial_boot_module_start", restored_again.machine.reached)

    def test_a_non_enumerating_build_is_reset_by_power_and_rolled_back(self):
        newer = self.newer_image()
        broken = self.session_module.with_overlay_code(newer, self.usb_overlay_elf,
                                                       "usb_wait_for_host", HANG)
        session = self.install_on_trial(broken)
        self.assertEqual(session.slot_image("slot_1"), self.slot_bytes(broken))
        hung = self.start(session, newer, vbus=True)
        machine = hung.machine
        self.assertEqual(hung.run_to_first_frame(), "condition met")
        machine.run(until=lambda: machine.usb_connect_count >= 1, until_ms=LIMIT_MS)
        machine.run(until_ms=machine.milliseconds() + 2000)
        self.assertEqual(self.trial_record(hung), (TRIAL_RUNNING, 2))
        self.assertNotIn("usb_session_storage_changed", machine.reached)
        pressed = machine.milliseconds()
        machine.hold_power(pressed, RESET_HOLD_MS + 2000)
        self.assertEqual(machine.run(until_ms=LIMIT_MS), "system reset requested")
        self.assertGreaterEqual(machine.milliseconds() - pressed, RESET_HOLD_MS - SCAN_MS)
        self.assertEqual(hung.slot_image("slot_2"), self.slot_bytes(self.image))
        again = self.start(hung, newer)
        self.assertEqual(again.run_to_first_frame(), "condition met")
        self.assertEqual(self.trial_record(again), (TRIAL_RUNNING, 3))
        rolling = self.start(again, newer)
        self.assertEqual(rolling.run_until_reset(), "system reset requested")
        self.assertEqual(rolling.slot_image("slot_1"), self.slot_bytes(self.image))
        self.assert_rollback_reported(rolling)
        summary("non-enumerating rollback", rolling)

    def escape_session(self, vbus=True, power_ms=5000, key=VOLUME_UP_KEY_ADC):
        session = self.session(vbus_at_start=vbus)
        machine = session.machine
        machine.adc_key_value = key
        machine.hold_power(0, power_ms)
        return session, machine

    def test_volume_up_and_power_held_with_a_cable_restart_into_the_usb_loader(self):
        session, machine = self.escape_session()
        self.assertEqual(session.run_until_reset(), "system reset requested")
        self.assert_healthy(session)
        self.assertEqual(machine.pmu.regs[LOADER_FLAG_REGISTER], LOADER_FLAG_VALUE & 0xFFFF)
        self.assertGreaterEqual(machine.milliseconds(), 2000)
        self.assertEqual(session.panel.lit_sweeps(), [])
        self.assertEqual(session.card.writes, [])
        self.assertEqual(machine.usb_connect_count, 0)

    def test_the_escape_keys_without_a_cable_or_let_go_early_start_normally(self):
        for vbus, power_ms, key in ((False, 5000, VOLUME_UP_KEY_ADC), (True, 1000, VOLUME_UP_KEY_ADC),
                                    (True, 5000, 1023)):
            session, machine = self.escape_session(vbus, power_ms, key)
            self.assertEqual(session.run_to_first_frame(), "condition met", (vbus, power_ms, key))
            self.assertEqual(machine.pmu_flag_writes, [], (vbus, power_ms, key))
            self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
