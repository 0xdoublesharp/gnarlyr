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
    def internal_volume(self, pack):
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
        volume = self.internal_volume(other_version_pack())
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

    def test_an_internal_pack_larger_than_the_window_loads_its_resident_text_and_each_module_segment(self):
        catalogue = lang_pack.read_catalogue()
        translations = {**lang_pack.pseudo_translations(catalogue), "STR_LANGUAGE_NAME": "Francais"}
        pack = lang_pack.build_pack("fr", catalogue, translations, lang_pack.read_segments(catalogue))
        self.assertGreater(len(pack), LANGUAGE_WINDOW)
        self.assertLessEqual(lang_pack.pack_info(pack)["memory"], LANGUAGE_WINDOW)
        card = self.with_card()
        session, machine = self.started_session(volume=self.internal_volume(pack),
                                                prepare=lambda session: card.insert(session.machine))
        self.assertTrue(self.loaded_language_pack(session, pack))
        slots = int.from_bytes(machine.read_memory(machine.symbols["slot_count"], 4), "little")
        self.assertEqual(64, slots)
        self.enter_settings(machine)
        self.assertTrue(self.loaded_language_pack(session, pack))
        self.assertTrue(self.loaded_language_segment(session, pack, SETTINGS_MODULE))
        self.assert_healthy(session)

    def test_an_internal_pack_of_another_version_is_skipped_for_the_same_built_in_language(self):
        pack = other_version_pack()
        session = self.session(volume=self.internal_volume(pack))
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(self.loaded_language_pack(session, lang_pack.language_pack("fr")))
        self.assertNotEqual(pack[:64], session.machine.read_memory(LANGUAGE_RAM, 64))
        self.assert_healthy(session)

    def test_the_language_descriptor_matches_the_built_in_image_bytes(self):
        path = f"/{IDENTITY.data_directory}/languages.bin"
        card = self.with_card()
        session, _ = self.started_session(prepare=lambda session: card.insert(session.machine))
        data = session.fat().read(path)
        self.assertFalse(card.fat().exists(path))
        descriptor = lang_pack.read_language_descriptor(data)
        expected = make_image.language_packs_in(self.image)
        self.assertEqual(list(descriptor["packs"]), ["en", "de", "es", "fr", "ja", "pt", "zh"])
        self.assertEqual(list(descriptor["packs"].values()), expected)
        self.assertEqual(descriptor["max_languages"], 16)
        self.assertEqual(descriptor["build"], f"{IDENTITY.version}-{self.build_tag()}")
        self.assertTrue(all(not lang_pack.pack_info(pack)["needs_extra_font"] for pack in expected))
        self.assertEqual(struct.unpack_from("<HHII", data, 4), (1, 16, 65535, 18432))
        self.assertEqual(struct.unpack_from("<II", data, 32), (7, len(data)))
        self.assertEqual(struct.unpack_from("<I", data, 40)[0],
                         lang_pack.fnv(data[44:], lang_pack.fnv(data[:40])))

    def test_a_descriptor_from_another_build_is_replaced(self):
        from echo_emulator.fat_volume import Fat32Volume
        volume = Fat32Volume()
        volume.add_directory(IDENTITY.data_directory)
        current = self.session()
        self.assertEqual(current.run_to_first_frame(), "condition met")
        path = f"/{IDENTITY.data_directory}/languages.bin"
        old = bytearray(current.fat().read(path))
        old[16:32] = b"old-build".ljust(16, b"\0")
        struct.pack_into("<I", old, 40, lang_pack.fnv(old[44:], lang_pack.fnv(old[:40])))
        volume.add_file("languages.bin", bytes(old), directory=IDENTITY.data_directory)
        fresh = self.session(volume=volume)
        self.assertEqual(fresh.run_to_first_frame(), "condition met")
        self.assertEqual(fresh.fat().read(path), current.fat().read(path))
        self.assert_only_the_user_volume_changed(fresh)

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
        self.assertTrue(self.loaded_language_pack(session, lang_pack.language_pack("fr")))
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
