import sys
import unittest

import test_echo_image as image_tests


class CodeSpaceTest(image_tests.EchoImageTest):
    damaged_process_slots = ()

    def session(self, *args, **kwargs):
        session = super().session(*args, **kwargs)
        for slot in self.damaged_process_slots:
            session.change_module(slot, 7, 600, lambda byte: byte ^ 0xFF)
        self.latest_session = session
        return session

    def test_debug_code_uses_the_second_processing_bank_after_a_bad_first_copy(self):
        self.damaged_process_slots = ("slot_1",)
        self.test_an_rtc_read_opens_i2c1_only_for_its_line_reads_without_writing_and_restores_the_bus()
        self.assertTrue(self.latest_session.module_reads("slot_1", 7))
        self.assertTrue(self.latest_session.module_reads("slot_2", 7))

    def test_without_processing_banks_a_script_aborts_and_reconnects_without_running_them(self):
        self.damaged_process_slots = ("slot_1", "slot_2")
        script = ("build %s\nrtc read\nreconnect\n" % self.build_tag()).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        executed = machine.count_window_code(0x0308F000, 0x3000)
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(executed[0], 0)
        self.assertEqual(machine.rtc_bus.frames, [])
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        self.assertEqual(self.read_report(session)["values"]["script.state"], "aborted")
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)


def main():
    arguments = image_tests.parse_arguments(sys.argv[1:])
    image_tests.CONFIG.update(image=arguments.image, elf=arguments.elf,
                              usb_overlay_elf=arguments.usb_overlay_elf)
    reason = image_tests.prerequisites_missing()
    if reason:
        print("SKIP: " + reason)
        return image_tests.SKIP_EXIT_CODE
    names = [name for name in CodeSpaceTest.__dict__ if name.startswith("test_")]
    suite = unittest.TestSuite(CodeSpaceTest(name) for name in names)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
