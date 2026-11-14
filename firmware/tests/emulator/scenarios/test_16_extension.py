import sys
import struct
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import echo_image_harness as image_tests

from echo_image_harness import (
    EchoImageHarness,
    fnv1a,
    main,
    make_image,
)


def headphone_bank(generation, trim, ceiling):
    profile = bytearray(148)
    profile[:8] = b"DSHP" + bytes((1, 0, 1, 0))
    profile[16:23] = b"Example"
    profile[40:43] = bytes((trim, ceiling, 25))
    struct.pack_into("<I", profile, 144, fnv1a(profile[:144]))
    block = bytearray(512)
    block[:4] = b"DSHF"
    struct.pack_into("<HHIIII", block, 4, 1, 0, generation, len(profile), fnv1a(profile), 0)
    struct.pack_into("<I", block, 24, fnv1a(block[:24]))
    block[28:28 + len(profile)] = profile
    return bytes(block)



class EchoImageTest(EchoImageHarness):
    damaged_effects_slots = ()
    damaged_extension_slots = ()

    def test_home_art_uses_the_active_ui_module_and_survives_a_settings_slide(self):
        from unicorn import UC_HOOK_CODE
        session = self.session()
        machine = session.machine
        draws = []
        for module in machine.modules:
            address = module.symbols.get("home_art_draw")
            if address is not None:
                def drew(uc, at, size, user, code=module):
                    if machine.read_memory(code.base, len(code.header)) == code.header:
                        draws.append(code.number)
                machine.uc.hook_add(UC_HOOK_CODE, drew, begin=address, end=address)
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertEqual(machine.run(until=lambda: draws, until_ms=machine.milliseconds() + image_tests.STARTUP_WORK_MS + 1000), "condition met")
        self.assertEqual({11}, set(draws))
        self.open_settings(machine)
        self.assertIn(2, draws)
        self.press(machine, image_tests.KEY_BACK)
        _, settings = self.module(machine, "settings")
        self.assertTrue(settings.loaded(machine))
        self.assertEqual(2, draws[-1])
        self.assert_healthy(session)

    def test_missing_playing_art_keeps_home_usable_without_repeated_failed_reads(self):
        session = self.session()
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, 11, 600, lambda byte: byte ^ 0xFF)
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(all(session.module_reads(slot, 11) for slot in ("slot_1", "slot_2")))
        machine = session.machine
        machine.run(until_ms=image_tests.STARTUP_SETTLED_MS)
        reads = [session.module_reads(slot, 11) for slot in ("slot_1", "slot_2")]
        machine.run(until_ms=machine.milliseconds() + 1500)
        self.assertEqual(reads, [session.module_reads(slot, 11) for slot in ("slot_1", "slot_2")])
        self.open_settings(machine)
        _, settings = self.module(machine, "settings")
        self.assertTrue(settings.loaded(machine))
        self.press(machine, image_tests.KEY_BACK)
        self.assertTrue(settings.loaded(machine))
        self.assertEqual(reads, [session.module_reads(slot, 11) for slot in ("slot_1", "slot_2")])
        self.assert_healthy(session)

    def test_usb_draws_its_backdrop_with_playing_art_unavailable(self):
        session = self.session(vbus=True)
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, 11, 600, lambda byte: byte ^ 0xFF)
        from unicorn import UC_HOOK_CODE
        machine = session.machine
        machine.note("usb_session_storage_changed")
        overlay = next(code for code in machine.overlays if code.name == "echo_usb_overlay")
        self.assertIn("home_art_draw_backdrop", overlay.symbols)
        address = overlay.symbols["home_art_draw_backdrop"]
        draws = []
        def drew(uc, at, size, user):
            draws.append(machine.read_memory(overlay.base, len(overlay.header)) == overlay.header)
        machine.uc.hook_add(UC_HOOK_CODE, drew, begin=address, end=address)
        self.assertEqual(session.run_to_first_frame(), "condition met")
        self.assertEqual(session.run_through_update_check(), "reached usb_session_storage_changed")
        self.assertTrue(draws)
        self.assertTrue(all(draws))
        self.assert_healthy(session)

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

    def session(self, *args, **kwargs):
        session = super().session(*args, **kwargs)
        for slot in self.damaged_effects_slots:
            session.change_module(slot, 12, 600, lambda byte: byte ^ 0xFF)
        for slot in self.damaged_extension_slots:
            session.change_module(slot, 8, 600, lambda byte: byte ^ 0xFF)
        self.latest_session = session
        return session

    def test_debug_code_uses_the_second_processing_bank_after_a_bad_first_copy(self):
        self.damaged_effects_slots = ("slot_1",)
        script = ("build %s\nrtc read\nreconnect\n" % self.build_tag()).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        self.assertEqual(self.read_report(session)["script"]["rtc"][0]["status"], "ok")
        self.assert_healthy(session)
        self.assertTrue(self.latest_session.module_reads("slot_1", 12))
        self.assertTrue(self.latest_session.module_reads("slot_2", 12))

    def test_without_processing_banks_a_script_aborts_and_reconnects_without_running_them(self):
        self.damaged_effects_slots = ("slot_1", "slot_2")
        script = ("build %s\nrtc read\nreconnect\n" % self.build_tag()).encode("ascii")
        session, machine = self.ejecting_session(
            self.debug_volume(run_txt=script),
            before_start=lambda machine: machine.stub_symbol(
                "extension_hal_clock_restore_wall", lambda machine: 0))
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        executed = machine.count_window_code(make_image.EFFECTS_MODULE_BASE,
                                             make_image.EFFECTS_MODULE_CAPACITY)
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(executed[0], 0)
        self.assertEqual(machine.rtc_bus.frames, [])
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        self.assertEqual(self.read_report(session)["values"]["script.state"], "aborted")
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_a_stale_time_file_waits_when_both_extension_copies_are_damaged(self):
        self.damaged_extension_slots = ("slot_1", "slot_2")
        volume = self.debug_volume()
        volume.add_file("time.bin", self.time_file(image_tests.NOVEMBER_2026, 0), None,
                        image_tests.IDENTITY.data_directory)
        session = self.session(volume=volume)
        session.machine.note("board_wall_time_set")
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertEqual(session.machine.reached.get("board_wall_time_set", 0), 0)
        self.assertTrue(session.fat().exists(image_tests.TIME_PATH))
        self.assert_healthy(session)

    def test_a_stale_time_file_is_removed_using_the_second_extension_copy(self):
        self.damaged_extension_slots = ("slot_1",)
        session, machine, sets = self.time_session(
            self.time_file(image_tests.NOVEMBER_2026, 0), written_before_power_on=True,
            rtc_valid=False)
        self.assertEqual(sets, [])
        self.assertFalse(session.fat().exists(image_tests.TIME_PATH))
        self.assert_healthy(session)
        self.assertTrue(self.latest_session.module_reads("slot_2", 8))

    def headphone_boot(self):
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2
        volume = self.debug_volume()
        good = headphone_bank(1, 12, 18)
        broken = bytearray(headphone_bank(2, 6, 24))
        broken[28 + 40] ^= 1
        data = good + broken
        volume.add_file("headphones.bin", data, None, image_tests.IDENTITY.data_directory)
        session = self.session(volume=volume)
        applied = []
        machine = session.machine
        machine.hook_symbol("hal_audio_set_headphones", lambda: applied.append(
            (machine.reg(UC_ARM_REG_R0), machine.reg(UC_ARM_REG_R1),
             machine.reg(UC_ARM_REG_R2) & 255)))
        self.assertEqual(session.run_to_home_screen(), "condition met")
        machine.run(until_ms=machine.milliseconds() + image_tests.STARTUP_WORK_MS)
        return session, applied, data

    def test_headphone_loading_uses_slot_two_and_the_last_intact_profile_bank(self):
        self.damaged_extension_slots = ("slot_1",)
        session, applied, data = self.headphone_boot()
        left, right, ceiling = applied[-1]
        self.assertTrue(0 < left < right < 65536)
        self.assertEqual(ceiling, 18)
        self.assertTrue(session.module_reads("slot_2", 8))
        path = "/" + image_tests.IDENTITY.data_directory + "/headphones.bin"
        self.assertEqual(session.fat().read(path), data)
        self.assert_healthy(session)

    def test_deferred_headphone_load_preserves_safe_start_and_profile_limits(self):
        from unicorn.arm_const import UC_ARM_REG_R0
        for comfort, ceiling, expected in ((2, 18, 13), (0, 18, 18), (2, 6, 6)):
            with self.subTest(comfort=comfort, ceiling=ceiling):
                volume = self.debug_volume()
                folder = image_tests.IDENTITY.data_directory
                volume.add_file("settings.bin",
                                image_tests.settings_file(0, 4096, b13=50, b38=comfort, b39=13),
                                directory=folder)
                volume.add_file("headphones.bin", headphone_bank(1, 0, ceiling) + bytes(512),
                                directory=folder)
                session = self.session(volume=volume)
                machine = session.machine
                levels = []
                machine.hook_symbol("hal_audio_set_volume",
                                    lambda: levels.append(machine.reg(UC_ARM_REG_R0)))
                self.assertEqual(session.run_to_home_screen(), "condition met")
                machine.run(until_ms=machine.milliseconds() + image_tests.STARTUP_WORK_MS)
                self.assertEqual(levels[-1], expected)
                self.assert_healthy(session)

    def test_missing_extension_cannot_bypass_a_saved_headphone_volume_ceiling(self):
        self.damaged_extension_slots = ("slot_1", "slot_2")
        session, applied, data = self.headphone_boot()
        self.assertEqual(applied[-1][2], 0)
        path = "/" + image_tests.IDENTITY.data_directory + "/headphones.bin"
        self.assertEqual(session.fat().read(path), data)
        self.assert_healthy(session)



if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
