import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    IDENTITY,
    MISSING_TEXT,
    STEPS_AFTER_NOTICE,
)


class EchoImageTest(EchoImageHarness):
    def test_the_text_is_read_from_the_language_pack_in_slot_1(self):
        session = self.session()
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(self.loaded_language_pack(session))
        self.assertTrue(session.language_pack_reads("slot_1"))
        self.assertEqual(session.language_pack_reads("slot_2"), [])
        self.assert_healthy(session)


    def test_a_damaged_language_pack_in_slot_1_is_replaced_by_the_copy_in_slot_2(self):
        session = self.session()
        session.damage_language_pack("slot_1")
        notices = session.machine.collect_notices()
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(self.loaded_language_pack(session))
        self.assertTrue(session.language_pack_reads("slot_1"))
        self.assertTrue(session.language_pack_reads("slot_2"))
        self.assertNotIn(MISSING_TEXT, [message for _, message in notices])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)


    def test_without_an_intact_language_pack_the_player_says_so_and_still_starts(self):
        session = self.session()
        session.damage_language_pack("slot_1")
        session.damage_language_pack("slot_2")
        machine = session.machine
        notices = machine.collect_notices()
        machine.answer_waits_for_key(1)
        machine.note("app_step")
        reason = machine.run(until=lambda: machine.reached.get("app_step", 0) >= STEPS_AFTER_NOTICE,
                             until_ms=self.session_module.FRAME_LIMIT_MS)
        self.assertEqual(reason, "condition met")
        self.assertEqual(notices[0], (IDENTITY.short_name, MISSING_TEXT))
        self.assertEqual(machine.reached["board_wait_for_key"], 1)
        self.assertFalse(self.loaded_language_pack(session))
        self.assertTrue(session.language_pack_reads("slot_1"))
        self.assertTrue(session.language_pack_reads("slot_2"))
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)



if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
