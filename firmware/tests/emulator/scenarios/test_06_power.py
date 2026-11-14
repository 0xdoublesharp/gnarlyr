import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    CABLE_STEADY_MS,
    COLD_REPORT_MS,
    DEBUG_FOLDER,
    IDENTITY,
    RESTART_PATH,
    RESTART_REQUEST,
    echo_debug,
    tempfile,
)


class EchoImageTest(EchoImageHarness):
    def test_a_paused_volume_change_is_saved_before_power_cut(self):
        from echo_image_harness import settings_file, KEY_PLAY, KEY_NEXT
        from echo_emulator.playback import Rig
        from echo_emulator.real_volume import real_volume
        from unicorn.arm_const import UC_ARM_REG_R0
        volume = real_volume(playable=True, album_count=1)
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("settings.bin", settings_file(0, 4096, b13=0),
                        directory=IDENTITY.data_directory)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        session, machine = rig.session, rig.machine
        levels = []
        machine.hook_symbol("hal_audio_set_volume", lambda: levels.append(machine.reg(UC_ARM_REG_R0)))
        rig.open_songs()
        rig.play_song(1)
        rig.press("play")
        rig.press_many("next", 3)
        self.assertEqual(levels[-1], 3)
        self.hold_power(machine)
        self.assertEqual(machine.run(until_ms=machine.milliseconds() + 8000),
                         "firmware reached hal_power_off")
        self.assertEqual(session.fat().read("/" + IDENTITY.data_directory + "/settings.bin")[13], 3)
        machine.continue_past_stop()
        self.assertEqual(machine.run(until_ms=machine.milliseconds() + 5000),
                         "firmware reached board_power_cut")
        self.assertEqual(session.fat().read("/" + IDENTITY.data_directory + "/settings.bin")[13], 3)
        cold = session.reboot()
        levels = []
        cold.machine.hook_symbol("hal_audio_set_volume",
                                 lambda: levels.append(cold.machine.reg(UC_ARM_REG_R0)))
        cold.run_to_home_screen()
        self.assertEqual(levels[-1], 3)
        self.press(cold.machine, KEY_PLAY)
        self.press(cold.machine, KEY_NEXT)
        self.assertEqual(levels[-1], 4)

    def test_a_long_power_press_saves_a_report_before_the_power_is_cut(self):
        session = self.session()
        machine = session.machine
        machine.note("debug_report_write")
        session.run_to_home_screen()
        machine.run(until_ms=COLD_REPORT_MS + 500)
        self.assertEqual(self.reports(session), [])
        self.hold_power(machine)
        self.assertEqual(machine.run(until_ms=machine.milliseconds() + 8000),
                         "firmware reached hal_power_off")
        self.assertNotIn("debug_report_write", machine.reached)
        machine.continue_past_stop()
        self.assertEqual(machine.run(until_ms=machine.milliseconds() + 5000),
                         "firmware reached board_power_cut")
        self.assertEqual(machine.reached["debug_report_write"], 1)
        self.assertTrue(session.usb_overlay_reads("slot_1"))
        values = self.read_report(session)["values"]
        self.assertEqual((values["report.sequence"], values["report.reason"],
                          values["report.cold_start"]), (1, "shutdown", 0))
        self.assertGreater(values["uptime_ms"], COLD_REPORT_MS)
        self.assertEqual(values["battery.source"], "gauge")
        self.assertEqual((values["power.vbus"], values["power.usb_powered"]), (0, 0))
        self.assertEqual(values["usb.grf_e8"], 0)
        self.assertIsNone(values["usb.gotgctl"])
        self.assertIsNone(values["usb.b_session"])
        details = [event["kind"] + " " + event["detail"] for event in self.read_report(session)["events"]]
        self.assertIn("key power long 1023", details)
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.panics, ["hal_power_off", "board_power_cut"])
        self.assert_only_the_user_volume_changed(session)

    def test_a_long_power_press_folds_the_wireframe_before_the_power_is_cut(self):
        session = self.session()
        machine = session.machine
        session.run_to_home_screen()
        machine.run(until_ms=COLD_REPORT_MS + 500)
        frames = machine.trace("wire_boot_fade")
        self.hold_power(machine)
        self.assertEqual(machine.run(until_ms=machine.milliseconds() + 8000),
                         "firmware reached hal_power_off")
        self.assertGreaterEqual(len(frames), 3)
        self.assertLessEqual(machine.milliseconds() - frames[0], 600)
        self.assertEqual(machine.faults, [])

    def test_switching_off_on_usb_power_saves_a_report_on_the_way_into_charge_mode(self):
        for screen_off in (False, True):
            with self.subTest(screen_off=screen_off):
                self.check_usb_charge_shutdown(screen_off)

    def test_a_cold_start_report_never_overwrites_the_report_before_it(self):
        first = self.session()
        machine = first.machine
        machine.note("usb_session_storage_changed")
        machine.answer_waits_for_key(4)
        first.run_to_home_screen()
        machine.run(until_ms=COLD_REPORT_MS + 500)
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(first, 1), "condition met")
        kept_path, kept = self.reports(first)[0]
        kept_bytes = first.fat().read(kept_path)
        self.assertEqual((kept["values"]["report.sequence"], kept["values"]["report.cold_start"]), (1, 0))
        second = self.cold_session_on(first)
        cold_path, cold = self.reports(second)[0]
        values = cold["values"]
        self.assertEqual((values["report.sequence"], values["report.cold_start"]), (2, 1))
        self.assertNotEqual(cold_path, kept_path)
        self.assertGreaterEqual(values["uptime_ms"], CABLE_STEADY_MS)
        self.assertLess(values["uptime_ms"], 2000)
        self.assertEqual(values["start.warm"], 0)
        self.assertEqual((values["usb.grf_e8_start"], values["usb.grf_e8"]), (2, 2))
        self.assertEqual(values["battery.source"], "gauge")
        self.assertEqual((values["battery.millivolts"], values["battery.usb_power"]), (3999, 1))
        self.assertEqual((values["power.adc_source"], values["power.adc_raw"]), ("gauge", 819))
        self.assertEqual(values["power.gauge_readings"], 1)
        self.assertEqual(values["power.gauge_millivolts"], 3999)
        third = self.cold_session_on(second)
        newest, older = self.reports(third)
        self.assertEqual((newest[0], newest[1]["values"]["report.sequence"]), (cold_path, 3))
        self.assertEqual(older[0], kept_path)
        self.assertEqual(third.fat().read(kept_path), kept_bytes)
        self.assertEqual(len(third.fat().list("/" + DEBUG_FOLDER)), 2)
        with tempfile.TemporaryDirectory() as directory:
            volume = Path(directory) / "volume"
            third.fat().extract("/" + DEBUG_FOLDER, volume / IDENTITY.data_directory / "debug")
            tool = echo_debug.Tool(volume=volume)
            status = tool.status()
            self.assertTrue(status["cold_start_only"])
            self.assertIn("advice", status)
            listed = tool.report()["reports"]
            self.assertEqual([(item["sequence"], item["cold_start"], item["reason"]) for item in listed],
                             [(3, True, "usb_session"), (1, False, "usb_session")])
        self.assert_only_the_user_volume_changed(third)
        self.assert_healthy(third)

    def test_a_stale_restart_request_is_removed_without_an_install(self):
        session, machine = self.ejecting_session(self.debug_volume(restart_req=RESTART_REQUEST))
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        machine.run(until_ms=machine.milliseconds() + 1000)
        self.assertFalse(session.fat().exists(RESTART_PATH))
        self.assertIsNone(machine.reset_requested)
        self.assertNotIn("board_restart", machine.reached)
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
