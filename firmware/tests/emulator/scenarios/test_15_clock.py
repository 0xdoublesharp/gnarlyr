import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    IDENTITY,
    NOVEMBER_2026,
    TIME_PATH,
    SETTINGS_PATH,
    struct,
)


class EchoImageTest(EchoImageHarness):
    def test_script_and_deferred_startup_rtc_reads_restore_the_bus(self):
        script = ("build %s\nrtc read\nreconnect\n" % self.build_tag()).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(machine.rtc_bus.frames, [])
        before = self.i2c1_state(machine)
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        read = self.read_report(session)["script"]["rtc"]
        self.assertEqual(read, [{"line": 2, "ms": read[0]["ms"], "action": "read", "written_s": None,
                                 "status": "ok", "registers": list(machine.rtc_bus.registers), "vl": 0,
                                 "time": "2026-01-03T12:00:05", "weekday": 6, "century": 0}])
        self.assertEqual(machine.rtc_bus.frames,
                         [("write", bytes([0xA2, 0x00])), ("read", 0xA3, 16)] * 2)
        self.assertEqual(self.i2c1_state(machine), before)
        self.assertIn(("grf", 0xBC, 0x000C0008), machine.register_log)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)


    def test_an_rtc_set_writes_the_synced_utc_time_clears_the_trust_flag_and_reads_it_back(self):
        script = ("build %s\nrtc set\nrtc read\nreconnect\n" % self.build_tag()).encode("ascii")
        volume = self.debug_volume(run_txt=script)
        volume.add_file("time.bin", self.time_file(NOVEMBER_2026, -480), None, IDENTITY.data_directory)
        session, machine = self.ejecting_session(volume, before_start=lambda machine: machine.stub_symbol(
            "board_wall_time_forget_file", lambda machine: 0))
        machine.rtc_bus.registers[2] |= 0x80
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        before = self.i2c1_state(machine)
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        written, read = self.read_report(session)["script"]["rtc"]
        self.assertEqual((written["action"], written["status"], written["vl"]), ("set", "ok", 0))
        self.assertTrue(NOVEMBER_2026 <= written["written_s"] <= NOVEMBER_2026 + 5, written)
        self.assertEqual((read["action"], read["status"], read["vl"]), ("read", "ok", 0))
        frame = machine.rtc_bus.frames[0]
        self.assertEqual((frame[0], frame[1][:2], len(frame[1])), ("write", bytes([0xA2, 0x02]), 9))
        self.assertEqual(frame[1][2] & 0x80, 0)
        self.assertNotIn(0x00, [entry[1][1] for entry in machine.rtc_bus.frames
                                if entry[0] == "write" and len(entry[1]) > 2])
        self.assertTrue(read["time"].startswith("2026-11-12T11:21"), read)
        self.assertEqual(self.i2c1_state(machine), before)
        self.assert_healthy(session)


    def test_settings_sets_the_clock_chip_and_refreshes_the_status_bar(self):
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP
        rig = self.playback_rig(prepare=lambda machine: machine.rtc_bus.registers.__setitem__(2, 0x85))
        machine = rig.machine
        sets, clocks = [], set()

        def wall_set():
            sets.append((machine.reg(UC_ARM_REG_R0), machine.reg(UC_ARM_REG_R1)))

        def status_draw():
            address = struct.unpack("<I", machine.read_memory(machine.reg(UC_ARM_REG_SP) + 8, 4))[0]
            if address:
                clocks.add(machine.read_memory(address, 24).split(b"\x00", 1)[0])

        machine.hook_symbol("board_wall_time_set", wall_set)
        machine.hook_symbol("status_bar_draw", status_draw)
        before = self.i2c1_state(machine)
        rig.press("previous")
        rig.press("play")
        rig.press_many("next", 8)
        rig.press("play")
        rig.press_many("menu", 4)
        rig.press("next")
        rig.press("play")
        expected = 1767225660
        self.assertEqual(sets, [(expected, 2 << 16)])
        self.assertEqual(list(machine.rtc_bus.registers[2:9]), [0, 1, 0, 1, 4, 1, 0x26])
        self.assertIn(b"00:01", clocks)
        self.assertEqual(self.i2c1_state(machine), before)
        self.assert_only_the_user_volume_changed(rig.session)
        self.assert_healthy(rig.session)


    def test_a_deferred_startup_read_restores_the_rtc_once(self):
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1
        session = self.session()
        machine = session.machine
        restored = []
        machine.hook_symbol("board_wall_time_set", lambda: restored.append(
            (machine.reg(UC_ARM_REG_R0), machine.reg(UC_ARM_REG_R1))))
        session.run_to_home_screen()
        machine.run(until=lambda: bool(restored), until_ms=machine.milliseconds() + 5000)
        self.assertEqual(len(restored), 1)
        self.assertEqual(restored[0][1], 2 << 16)
        self.assertEqual(machine.rtc_bus.frames,
                         [("write", bytes([0xA2, 0x00])), ("read", 0xA3, 16)])
        self.assert_healthy(session)


    def test_a_settings_time_and_offset_survive_a_cold_restart(self):
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP
        rig = self.playback_rig(prepare=lambda machine: machine.rtc_bus.registers.__setitem__(2, 0x85))
        rig.press("previous")
        rig.press("play")
        rig.press_many("next", 8)
        rig.press("play")
        rig.press_many("menu", 4)
        rig.press("next")
        rig.press("menu")
        rig.press("next")
        rig.press("play")
        saved = rig.session.fat().read(SETTINGS_PATH)
        self.assertEqual(struct.unpack_from("<h", saved, 49)[0], 15)
        before = bytes(rig.machine.rtc_bus.registers)
        second = rig.session.reboot()
        machine = second.machine
        restored, clocks = [], set()
        machine.hook_symbol("board_wall_time_set", lambda: restored.append(
            (machine.reg(UC_ARM_REG_R0), machine.reg(UC_ARM_REG_R1))))

        def status_draw():
            address = struct.unpack("<I", machine.read_memory(machine.reg(UC_ARM_REG_SP) + 8, 4))[0]
            if address:
                clocks.add(machine.read_memory(address, 24).split(b"\x00", 1)[0])

        machine.hook_symbol("status_bar_draw", status_draw)
        self.assertEqual(second.run_to_home_screen(), "condition met")
        self.assertEqual(restored, [])
        self.assertEqual(machine.run(until=lambda: b"00:01" in clocks,
                                     until_ms=machine.milliseconds() + 5000), "condition met")
        self.assertEqual(restored, [(1767224760, (2 << 16) | 15)])
        self.assertEqual(bytes(machine.rtc_bus.registers), before)
        self.assertEqual(machine.rtc_bus.frames,
                         [("write", bytes([0xA2, 0x00])), ("read", 0xA3, 16)])
        self.assert_only_the_user_volume_changed(second)
        self.assert_healthy(second)


    def test_a_missing_startup_rtc_stays_unknown_and_returns_without_retrying(self):
        session = self.session()
        machine = session.machine
        machine.rtc_bus.present = False
        sets = machine.trace("board_wall_time_set")
        reads = machine.trace("extension_hal_clock_restore_wall")
        steps = machine.trace("app_step")
        self.assertEqual(session.run_to_home_screen(), "condition met")
        before = self.i2c1_state(machine)
        self.assertEqual(machine.run(until=lambda: bool(machine.rtc_bus.frames),
                                     until_ms=machine.milliseconds() + 5000), "condition met")
        count = len(steps)
        self.assertEqual(machine.run(until=lambda: len(steps) > count,
                                     until_ms=machine.milliseconds() + 70), "condition met")
        self.assertLessEqual(steps[-1] - reads[0], 60)
        machine.run(until_ms=machine.milliseconds() + 1000)
        self.assertEqual(sets, [])
        self.assertEqual(len(reads), 1)
        self.assertEqual(machine.rtc_bus.frames, [("write", bytes([0xA2, 0x00]))])
        self.assertEqual(self.i2c1_state(machine), before)
        self.assert_healthy(session)


    def test_rtc_restore_keeps_boot_and_first_audio_timing_and_waits_for_stopped_audio(self):
        from unicorn.arm_const import UC_ARM_REG_R0
        results = []
        for enabled in (False, True):
            frames = []

            def prepare(machine):
                machine.note("board_wall_time_set")
                if not enabled:
                    def read_without_bus(machine):
                        machine.uc.mem_write(machine.reg(UC_ARM_REG_R0),
                                             bytes([0]) + bytes(machine.rtc_bus.registers))
                    machine.stub_symbol("hal_system_rtc_read", read_without_bus)
                machine.hook_symbol("app_step", lambda: frames.append(machine.milliseconds())
                                    if len(machine.panel.lit_sweeps()) == 1 and not frames else None)

            rig = self.playback_rig(prepare=prepare)
            self.assertEqual(rig.machine.reached.get("board_wall_time_set", 0), 1)
            rig.open_songs()
            started = rig.play_song(1)
            audible = rig.first_audio_after(started)
            self.assertIsNotNone(audible)
            self.assertEqual(rig.audio.underruns(started), [])
            self.assertEqual(rig.audio.output_underruns(), 0)
            results.append((frames[0], audible - started))
            self.assert_healthy(rig.session)
        print("\n[RTC timing] instant-read boot/audio=%s ms; bus-read boot/audio=%s ms" % tuple(results))
        self.assertLessEqual(abs(results[0][0] - results[1][0]), 1)
        self.assertLessEqual(abs(results[0][1] - results[1][1]), 5)
        scheduled = [False]

        def delay_restore(machine):
            def delay_once():
                if not scheduled[0]:
                    machine.uc.mem_write(machine.symbols["restore_due_ms"],
                                         struct.pack("<I", machine.milliseconds() + 60000))
                    scheduled[0] = True
            machine.hook_symbol("app_step", delay_once)

        rig = self.playback_rig(prepare=delay_restore)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertEqual(rig.machine.rtc_bus.frames, [])
        rig.machine.uc.mem_write(rig.machine.symbols["restore_due_ms"],
                                 struct.pack("<I", rig.machine.milliseconds()))
        rig.run_for(500)
        self.assertEqual(rig.machine.rtc_bus.frames, [])
        self.assertEqual(rig.audio.underruns(started), [])
        self.assertEqual(rig.audio.output_underruns(), 0)
        self.assert_healthy(rig.session)


    def test_an_rtc_read_without_an_answer_reports_no_ack_and_restores_the_bus(self):
        script = ("build %s\nrtc read\nreconnect\n" % self.build_tag()).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        machine.rtc_bus.present = False
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        before = self.i2c1_state(machine)
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        self.assertEqual([(read["status"], read["registers"]) for read in report["script"]["rtc"]],
                         [("no_ack", None)])
        self.assertEqual(report["script"]["lines"][1]["status"], "failed")
        self.assertEqual(machine.rtc_bus.frames, [("write", bytes([0xA2, 0x00]))] * 2)
        self.assertEqual(self.i2c1_state(machine), before)
        self.assert_healthy(session)


    def test_an_ejected_session_with_a_time_file_sets_the_clock_and_removes_the_file(self):
        session, machine, sets = self.time_session(self.time_file(NOVEMBER_2026, -480))
        self.assertEqual(len(sets), 1)
        utc, packed = sets[0]
        self.assertEqual(utc, NOVEMBER_2026)
        self.assertEqual(struct.unpack_from("<h", session.fat().read(SETTINGS_PATH), 49)[0], -480)
        self.assertEqual(struct.unpack("<hBx", struct.pack("<I", packed)), (-480, 1))
        self.assertFalse(session.fat().exists(TIME_PATH))
        self.assert_healthy(session)


    def test_a_damaged_time_file_is_removed_without_setting_the_clock(self):
        damaged = bytearray(self.time_file(NOVEMBER_2026, 60))
        damaged[9] ^= 1
        session, machine, sets = self.time_session(bytes(damaged), rtc_valid=False)
        self.assertEqual(sets, [])
        self.assertFalse(session.fat().exists(TIME_PATH))
        self.assert_healthy(session)


    def test_a_time_file_from_an_ejected_session_dates_the_next_play_in_the_listening_log(self):
        from echo_emulator.playback import ALBUM_COUNT, Rig
        from echo_emulator.real_volume import real_volume
        script = "\n".join(["build " + self.build_tag(), "play", "down,down,down,down,down,down,down",
                            "play", "play", "wait 3000", "reconnect", ""])
        volume = self.debug_volume(real_volume(playable=True, album_count=ALBUM_COUNT),
                                   run_txt=script.encode("ascii"))
        volume.add_file("time.bin", self.time_file(NOVEMBER_2026, -480), None, IDENTITY.data_directory)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume,
                  module_elfs=self.module_elfs,
                  prepare=lambda machine: machine.stub_symbol("board_wall_time_forget_file",
                                                              lambda machine: 0))
        session, machine = rig.session, rig.machine
        for name in ("usb_session_storage_changed", "debug_script_take", "hal_system_usb_reconnect"):
            machine.note(name)
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        set_ms = machine.milliseconds()
        self.assertFalse(session.fat().exists(TIME_PATH))
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertTrue(rig.audio.plays)
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        listens = session.fat().read("/" + IDENTITY.data_directory + "/listens.bin")
        record = listens[1024 + 64:1024 + 128]
        sequence, clock = struct.unpack_from("<I", record, 0)[0], struct.unpack_from("<I", record, 36)[0]
        flags, offset = record[49], struct.unpack_from("<h", record, 50)[0]
        self.assertEqual(sequence, 1)
        self.assertEqual((flags, offset), (1, -480))
        elapsed_s = (machine.milliseconds() - set_ms) // 1000
        self.assertTrue(NOVEMBER_2026 <= clock <= NOVEMBER_2026 + elapsed_s + 1, (clock, elapsed_s))
        self.assertEqual(listens[4], 1)
        self.assert_healthy(session)


    def test_a_time_file_already_there_at_power_on_is_removed_and_never_applied(self):
        session, machine, sets = self.time_session(self.time_file(NOVEMBER_2026, 0),
                                                   written_before_power_on=True, rtc_valid=False)
        self.assertEqual(sets, [])
        self.assertFalse(session.fat().exists(TIME_PATH))
        self.assert_healthy(session)



if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
