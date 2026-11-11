import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    make_image,
)


class EchoImageTest(EchoImageHarness):
    def test_a_damaged_extension_module_in_slot_1_is_replaced_by_the_copy_in_slot_2(self):
        session = self.session()
        number = make_image.EXTENSION_MODULE_NUMBER
        session.change_module("slot_1", number, 600, lambda byte: byte ^ 0xFF)
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(self.extension_loaded(session))
        self.assertTrue(session.module_reads("slot_1", number))
        self.assertTrue(session.module_reads("slot_2", number))
        reads = [session.module_reads(slot, number) for slot in ("slot_1", "slot_2")]
        session.machine.run(until_ms=session.machine.milliseconds() + 1500)
        self.assertEqual(reads, [session.module_reads(slot, number) for slot in ("slot_1", "slot_2")])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_without_an_intact_extension_module_the_player_starts_and_none_of_it_runs(self):
        session = self.session()
        number = make_image.EXTENSION_MODULE_NUMBER
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, number, 600, lambda byte: byte ^ 0xFF)
        machine = session.machine
        executed = machine.count_window_code(make_image.EXTENSION_MODULE_BASE,
                                             make_image.EXTENSION_MODULE_CAPACITY)
        self.assertEqual(session.run_to_home_screen(), "condition met")
        machine.run(until_ms=machine.milliseconds() + 1500)
        self.assertEqual(executed[0], 0)
        self.assertFalse(any(machine.read_memory(make_image.EXTENSION_MODULE_BASE,
                                                 make_image.EXTENSION_MODULE_CAPACITY)))
        self.assertTrue(session.module_reads("slot_1", number))
        self.assertTrue(session.module_reads("slot_2", number))
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
