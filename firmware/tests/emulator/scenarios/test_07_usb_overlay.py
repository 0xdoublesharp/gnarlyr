import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    IDENTITY,
    english,
    make_image,
)


class EchoImageTest(EchoImageHarness):
    def test_a_second_usb_session_reuses_the_usb_overlay_still_in_memory(self):
        session = self.session()
        machine = session.machine
        for name in ("usb_storage_session", "usb_session_storage_changed",
                     "hal_system_return_scratch"):
            machine.note(name)
        machine.answer_waits_for_key(4)
        executed = machine.count_window_code()
        machine.run(until=lambda: "hal_system_return_scratch" in machine.reached,
                    until_ms=self.session_module.FRAME_LIMIT_MS)
        self.assertGreater(executed[0], 0)
        self.assertEqual(session.usb_overlay_reads("slot_1"), [])
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        loads = session.usb_overlay_reads("slot_1")
        self.assertTrue(loads)
        machine.run(until_ms=machine.milliseconds() + 200)
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        self.assertEqual(machine.reached["usb_storage_session"], 2)
        self.assertEqual(session.usb_overlay_reads("slot_1"), loads)
        self.assertEqual(session.usb_overlay_reads("slot_2"), [])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_a_damaged_usb_overlay_in_slot_1_is_replaced_by_the_copy_in_slot_2(self):
        session = self.session(files=[(IDENTITY.update_file, self.image, None)], vbus=True)
        session.damage_usb_overlay("slot_1")
        machine = session.machine
        machine.note("updater_check")
        session.run_to_first_frame()
        self.assertEqual(session.run_through_update_check(),
                         "reached usb_session_storage_changed")
        self.assertEqual(machine.reached["updater_check"], 1)
        self.assertTrue(session.usb_overlay_reads("slot_1"))
        self.assertTrue(session.usb_overlay_reads("slot_2"))
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_without_intact_usb_code_the_session_says_so_and_nothing_from_it_runs(self):
        session = self.session(files=[(IDENTITY.update_file, self.newer_image(), None)], vbus=True)
        session.damage_usb_overlay("slot_1")
        session.damage_usb_overlay("slot_2")
        machine = session.machine
        for name in ("updater_check", "updater_install_trial", "usb_storage_session", "usb_power_down"):
            machine.note(name)
        notices = machine.collect_notices()
        session.run_to_first_frame()
        machine.run(until=lambda: session.usb_overlay_reads("slot_1"),
                    until_ms=self.session_module.FRAME_LIMIT_MS)
        executed = machine.count_window_code()
        self.assertEqual(session.run_until_waiting_for_key(), "reached board_wait_for_key")
        self.assertEqual(notices[-1], (english("STR_USB"), english("STR_USB_CODE_MISSING")))
        self.assertGreaterEqual(machine.reached["usb_power_down"], 1)
        for name in ("updater_check", "updater_install_trial", "usb_storage_session"):
            self.assertNotIn(name, machine.reached)
        self.assertEqual(executed[0], 0)
        self.assertFalse(any(machine.read_memory(make_image.OVERLAY_BASE,
                                                 make_image.OVERLAY_CAPACITY)))
        self.assertTrue(session.file_exists(IDENTITY.update_file))
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
