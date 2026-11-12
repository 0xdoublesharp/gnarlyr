import sys
import struct
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    IDENTITY,
    MISSING_TEXT,
    STEPS_AFTER_NOTICE,
    LANGUAGE_RAM,
    KEY_PLAY,
    KEY_PREVIOUS,
    lang_pack,
    make_image,
    TRIAL_RUNNING,
)

LANGUAGE_WINDOW = 18432
SETTINGS_MODULE = 2
SETTINGS_SEGMENT = 2
TRIAL_CHECK = 0x5A


def other_version_pack():
    catalogue = lang_pack.read_catalogue()
    segments = lang_pack.read_segments(catalogue)
    pack = bytearray(lang_pack.build_pack("fr", catalogue, {"STR_LANGUAGE_NAME": "Francais"}, segments))
    struct.pack_into("<H", pack, 4, lang_pack.VERSION + 1)
    return lang_pack.sealed(pack, lang_pack.index_size(len(segments)))


class EchoImageTest(EchoImageHarness):
    def card_volume(self, pack):
        from echo_emulator.fat_volume import Fat32Volume
        volume = Fat32Volume()
        folder = IDENTITY.data_directory
        volume.add_directory(folder)
        volume.add_directory("lang", directory=folder)
        volume.add_file("fr.pack", pack, directory=folder + "/lang")
        settings = bytearray(self.saved_settings(0, 15, 0))
        settings[19:24] = b"fr\0\0\0"
        struct.pack_into("<I", settings, 54, lang_pack.fnv(settings[:54]))
        volume.add_file("settings.bin", settings, directory=folder)
        return volume

    def trial_volume(self):
        volume = self.card_volume(other_version_pack())
        folder = IDENTITY.data_directory
        record = bytearray(512)
        record[:8] = b"DSUT" + bytes((TRIAL_RUNNING, 0, TRIAL_RUNNING ^ TRIAL_CHECK, 1))
        volume.add_file("trial.bin", bytes(record), directory=folder)
        return volume

    def enter_settings(self, machine):
        _, settings = self.module(machine, "settings")
        self.press(machine, KEY_PREVIOUS)
        self.release_to_first_draw_ms(machine, KEY_PLAY)
        self.assertTrue(settings.loaded(machine))

    def test_a_card_pack_larger_than_the_window_loads_its_resident_text_and_each_module_segment(self):
        catalogue = lang_pack.read_catalogue()
        translations = {**lang_pack.pseudo_translations(catalogue), "STR_LANGUAGE_NAME": "Francais"}
        pack = lang_pack.build_pack("fr", catalogue, translations, lang_pack.read_segments(catalogue))
        self.assertGreater(len(pack), LANGUAGE_WINDOW)
        self.assertLessEqual(lang_pack.pack_info(pack)["memory"], LANGUAGE_WINDOW)
        session, machine = self.started_session(volume=self.card_volume(pack))
        self.assertTrue(self.loaded_language_pack(session, pack))
        slots = int.from_bytes(machine.read_memory(machine.symbols["slot_count"], 4), "little")
        self.assertEqual(64, slots)
        self.enter_settings(machine)
        self.assertTrue(self.loaded_language_pack(session, pack))
        self.assertTrue(self.loaded_language_segment(session, pack, SETTINGS_MODULE))
        self.assert_healthy(session)

    def test_a_card_pack_of_another_version_is_skipped_for_the_language_of_the_image(self):
        pack = other_version_pack()
        session = self.session(volume=self.card_volume(pack))
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(self.loaded_language_pack(session))
        self.assertNotEqual(pack[:64], session.machine.read_memory(LANGUAGE_RAM, 64))
        self.assert_healthy(session)

    def test_a_damaged_module_segment_in_slot_1_is_replaced_by_the_copy_in_slot_2(self):
        session, machine = self.started_session(
            prepare=lambda session: session.damage_language_segment("slot_1", SETTINGS_SEGMENT))
        self.assertTrue(self.loaded_language_pack(session))
        self.assertEqual(session.language_pack_reads("slot_2"), [])
        self.enter_settings(machine)
        self.assertTrue(session.language_pack_reads("slot_2"))
        pack = make_image.language_packs_in(self.image)[0]
        self.assertTrue(self.loaded_language_segment(session, pack, SETTINGS_MODULE))
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_the_text_is_read_from_the_language_pack_in_slot_1(self):
        session = self.session()
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(self.loaded_language_pack(session))
        self.assertTrue(session.language_pack_reads("slot_1"))
        self.assertEqual(session.language_pack_reads("slot_2"), [])
        self.assert_healthy(session)

    def test_a_trial_start_reads_slot_1_text_with_the_emmc_never_reset_after_its_start(self):
        session = self.session(volume=self.trial_volume())
        session.damage_language_pack("slot_2")
        notices = session.machine.collect_notices()
        self.assertEqual(session.run_to_home_screen(), "condition met")
        emmc = session.machine.emmc_controller
        self.assertEqual([commands for _, commands in emmc.resets if commands > 0], [])
        self.assertNotIn(MISSING_TEXT, [message for _, message in notices])
        self.assertTrue(self.loaded_language_pack(session))
        self.assertEqual(self.trial_record(session), (TRIAL_RUNNING, 1))
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
