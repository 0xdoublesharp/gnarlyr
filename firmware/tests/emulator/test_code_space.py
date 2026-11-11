import sys
import struct
import unittest

import test_echo_image as image_tests


def hash_bytes(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def headphone_bank(generation, trim, ceiling):
    profile = bytearray(148)
    profile[:8] = b"DSHP" + bytes((1, 0, 1, 0))
    profile[16:23] = b"Example"
    profile[40:43] = bytes((trim, ceiling, 25))
    struct.pack_into("<I", profile, 144, hash_bytes(profile[:144]))
    block = bytearray(512)
    block[:4] = b"DSHF"
    struct.pack_into("<HHIIII", block, 4, 1, 0, generation, len(profile), hash_bytes(profile), 0)
    struct.pack_into("<I", block, 24, hash_bytes(block[:24]))
    block[28:28 + len(profile)] = profile
    return bytes(block)


class CodeSpaceTest(image_tests.EchoImageTest):
    damaged_process_slots = ()
    damaged_extension_slots = ()

    def session(self, *args, **kwargs):
        session = super().session(*args, **kwargs)
        for slot in self.damaged_process_slots:
            session.change_module(slot, 7, 600, lambda byte: byte ^ 0xFF)
        for slot in self.damaged_extension_slots:
            session.change_module(slot, 8, 600, lambda byte: byte ^ 0xFF)
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
        self.test_a_time_file_already_there_at_power_on_is_removed_and_never_applied()
        self.assertTrue(self.latest_session.module_reads("slot_2", 8))

    def test_about_uses_the_second_views_copy_when_the_first_is_damaged(self):
        session, machine = self.started_session()
        session.change_module("slot_1", 1, 600, lambda byte: byte ^ 0xFF)
        self.open_settings(machine)
        self.press(machine, image_tests.KEY_PLAY)
        _, views = self.module(machine, "views")
        self.assertTrue(views.loaded(machine))
        self.assertTrue(session.module_reads("slot_2", 1))
        self.press(machine, image_tests.KEY_BACK)
        _, settings = self.module(machine, "settings")
        self.assertTrue(settings.loaded(machine))
        self.assert_healthy(session)

    def test_missing_views_keeps_about_shut_and_settings_usable(self):
        session, machine = self.started_session()
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, 1, 600, lambda byte: byte ^ 0xFF)
        toasts = self.collect_toasts(machine)
        self.open_settings(machine)
        self.press(machine, image_tests.KEY_PLAY)
        self.assertEqual(toasts, [image_tests.english("STR_SCREEN_MISSING")])
        self.press(machine, image_tests.KEY_NEXT)
        self.press(machine, image_tests.KEY_PLAY)
        _, settings = self.module(machine, "settings")
        self.assertTrue(settings.loaded(machine))
        self.assert_healthy(session)


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

    def test_missing_extension_cannot_bypass_a_saved_headphone_volume_ceiling(self):
        self.damaged_extension_slots = ("slot_1", "slot_2")
        session, applied, data = self.headphone_boot()
        self.assertEqual(applied[-1][2], 0)
        path = "/" + image_tests.IDENTITY.data_directory + "/headphones.bin"
        self.assertEqual(session.fat().read(path), data)
        self.assert_healthy(session)


    def test_unavailable_flac_core_refuses_the_track_without_starting_output(self):
        rig = self.playback_rig()
        rig.machine.stub_symbol("board_core1_start_stream", lambda machine: 0)
        starts = rig.machine.trace("dmac_start_loop")
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(500)
        self.assertIsNone(rig.first_audio_after(mark))
        self.assertEqual(starts, [])
        self.assert_healthy(rig.session)


    def damage_flac_program(self, session, slot):
        lba = session.layout[slot] + image_tests.make_image.CORE1_FLAC_OFFSET // 512 + 1
        sector = bytearray(session.disk.read(lba, 1))
        sector[40] ^= 0xFF
        session.disk.write(lba, bytes(sector))
        session.before = session.disk.snapshot()

    def test_flac_plays_from_the_second_core_program_copy_after_a_bad_crc(self):
        rig = self.playback_rig()
        self.damage_flac_program(rig.session, "slot_1")
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(500)
        self.assertIsNotNone(rig.first_audio_after(mark), {
            "active": rig.audio.core1.active, "starts": rig.audio.core1.starts,
            "requested": rig.audio.requested_program,
            "control": struct.unpack("<31I", rig.machine.read_memory(0x01020100, 124)),
            "core_pc": hex(rig.audio.core1.core.pc) if rig.audio.core1.core else None,
            "loaded": rig.machine.read_memory(0x01000000, 8).hex(),
            "faults": rig.machine.faults,
        })
        self.assertTrue(rig.audio.core1.active)
        self.assertIn(0, rig.audio.core1.started)
        self.assertEqual(rig.audio.underruns(mark), [])
        self.assert_healthy(rig.session)

    def test_two_bad_flac_program_copies_refuse_the_track_without_core_execution(self):
        rig = self.playback_rig()
        for slot in ("slot_1", "slot_2"):
            self.damage_flac_program(rig.session, slot)
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(500)
        self.assertIsNone(rig.first_audio_after(mark))
        self.assertEqual(rig.audio.core1.starts, 0)
        self.assert_healthy(rig.session)


    def test_arm_rescan_keeps_legacy_half_millisecond_durations(self):
        def wave(frames):
            pcm = bytes(frames * 2)
            return struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                               16, 1, 1, 48000, 96000, 2, 16, b"data", len(pcm)) + pcm
        files = [("EVEN.WAV", wave(24024), None), ("ODD.WAV", wave(24072), None),
                 ("WHOLE.WAV", wave(48000), None)]
        session = self.session(files=files)
        session.machine.stop_at("hal_system_return_scratch")
        self.assertEqual(session.machine.run(until_ms=self.session_module.FRAME_LIMIT_MS),
                         "reached hal_system_return_scratch")
        database = session.fat().read("/" + image_tests.IDENTITY.data_directory + "/library.db")
        offset, count = struct.unpack_from("<II", database, 8)
        self.assertEqual(count, 3)
        durations = [struct.unpack_from("<I", database, offset + index * 32 + 16)[0]
                     for index in range(count)]
        self.assertEqual(sorted(durations), [500, 501, 1000])
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
    names = arguments.tests or [name for name in CodeSpaceTest.__dict__ if name.startswith("test_")]
    suite = unittest.TestSuite(CodeSpaceTest(name) for name in names)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
