import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    IDENTITY,
    NOVEMBER_2026,
    TIME_PATH,
    struct,
)


class EchoImageTest(EchoImageHarness):
    def test_an_rtc_read_opens_i2c1_only_for_its_line_reads_without_writing_and_restores_the_bus(self):
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
        self.assertEqual(machine.rtc_bus.frames, [("write", bytes([0xA2, 0x00])), ("read", 0xA3, 16)])
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
        self.assertEqual(machine.rtc_bus.frames, [("write", bytes([0xA2, 0x00]))])
        self.assertEqual(self.i2c1_state(machine), before)
        self.assert_healthy(session)

    def test_an_ejected_session_with_a_time_file_sets_the_clock_and_removes_the_file(self):
        session, machine, sets = self.time_session(self.time_file(NOVEMBER_2026, -480))
        self.assertEqual(len(sets), 1)
        utc, packed = sets[0]
        self.assertEqual(utc, NOVEMBER_2026)
        self.assertEqual(struct.unpack("<hBx", struct.pack("<I", packed)), (-480, 1))
        self.assertFalse(session.fat().exists(TIME_PATH))
        self.assert_healthy(session)

    def test_a_damaged_time_file_is_removed_without_setting_the_clock(self):
        damaged = bytearray(self.time_file(NOVEMBER_2026, 60))
        damaged[9] ^= 1
        session, machine, sets = self.time_session(bytes(damaged))
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
        self.assertEqual(listens[4], 2)
        self.assert_healthy(session)

    def test_a_time_file_already_there_at_power_on_is_removed_and_never_applied(self):
        session, machine, sets = self.time_session(self.time_file(NOVEMBER_2026, 0),
                                                   written_before_power_on=True)
        self.assertEqual(sets, [])
        self.assertFalse(session.fat().exists(TIME_PATH))
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
