import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    TRIAL_RUNNING,
    EchoImageHarness,
    main,
    BACKLIGHT_PIN,
    BOOT_FLAG_CLEAR,
    COLD_REPORT_MS,
    DEBUG_FOLDER,
    CONNECT_KEYS,
    GPIO_DATA,
    HEIGHT,
    IDENTITY,
    KEY_HOLD_MS,
    KEY_PLAY,
    LOADER_SELECT_REGISTER,
    LOADER_SELECT_VALUE,
    QUIET_AFTER_MS,
    RECONNECT_OFF_MS,
    RECONNECT_WAIT_MS,
    RESTART_ANSWER,
    USB_SCREEN_CAPTURES,
    RESTART_PATH,
    RESTART_REQUEST,
    RUN_PATH,
    SOAK_MINUTE_MS,
    WIDTH,
    echo_debug,
    struct,
    tempfile,
)


class EchoImageTest(EchoImageHarness):
    def test_a_script_runs_once_after_an_eject_plays_a_track_and_the_player_reconnects(self):
        from PIL import Image
        from echo_emulator.playback import ALBUM_COUNT, Rig
        from echo_emulator.real_volume import real_volume
        script = "\n".join(["build " + self.build_tag(), "play", "down,down,down,down,down,down,down",
                            "play", "play", "wait 1500", "measure 1500", "back:long", "wait 700",
                            "screenshot home", "reconnect", "play", ""])
        volume = self.debug_volume(real_volume(playable=True, album_count=ALBUM_COUNT),
                                   run_txt=script.encode("ascii"))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume,
                  module_elfs=self.module_elfs)
        session, machine = rig.session, rig.machine
        marks, frames = [], []
        for name in ("usb_session_storage_changed", "debug_script_take", "hal_system_usb_reconnect"):
            machine.note(name)
        for name in ("debug_report_write", "debug_script_take", "hal_system_usb_reconnect"):
            machine.hook_symbol(name, lambda name=name: marks.append((machine.milliseconds(), name)))
        machine.hook_symbol("hal_system_usb_reconnect",
                            lambda: frames.append(list(machine.panel.frame)))
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertFalse(session.fat().exists(RUN_PATH))
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(machine.usb_connect_count, 1)
        self.assertTrue(rig.audio.plays)
        self.assertIsNotNone(rig.first_audio_after(rig.audio.plays[0]))
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertGreaterEqual(self.log_time(machine, "pull-up on", 1)
                                - self.log_time(machine, "soft disconnect"), RECONNECT_OFF_MS)
        report = self.read_report(session)
        values, ran = report["values"], report["script"]
        self.assertEqual(values["script.state"], "finished")
        self.assertEqual(values["script.interrupted_line"], 0)
        self.assertEqual([line["status"] for line in ran["lines"]], ["ok"] * 11 + ["not_run"])
        self.assertEqual([line["text"] for line in ran["lines"]], script.split("\n")[:12])
        measure = ran["measures"][0]
        self.assertEqual((measure["line"], measure["ms"], measure["audio"]), (7, 1500, 1))
        self.assertGreater(measure["frames"], 0)
        self.assertGreater(measure["frame_avg_us"], 0)
        self.assertGreater(measure["blocks"], 0)
        self.assertEqual(measure["underruns"], 0)
        self.assertGreater(measure["buffered_ms"], 0)
        self.assertGreater(measure["handoff_avg_us"], 0)
        self.assertTrue(0 <= measure["core0_busy_percent"] <= 100)
        self.assertIn("core1_busy_permille", measure)
        self.assertEqual(ran["screenshots"], [{"slot": 0, "name": "home"}])
        details = [event["kind"] + " " + event["detail"] for event in report["events"]]
        self.assertIn("script finished", details)
        self.assertTrue(any(detail.startswith("player playing") for detail in details))
        with tempfile.TemporaryDirectory() as directory:
            shots = self.converted_screenshots(session, directory)
            self.assertEqual([(shot["name"], shot["width"], shot["height"]) for shot in shots],
                             [("home", WIDTH, HEIGHT)])
            picture = Image.open(shots[0]["png"]).convert("RGB")
            drawn = echo_debug.rgb888(struct.pack("<%dH" % len(frames[0]), *frames[0]), len(frames[0]))
            self.assertEqual(picture.size, (WIDTH, HEIGHT))
            self.assertEqual(picture.tobytes(), drawn)
            self.assertGreater(len(set(frames[0])), 16)
        machine.run(until_ms=machine.milliseconds() + QUIET_AFTER_MS)
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertEqual(machine.reached["hal_system_usb_reconnect"], 1)
        self.assertEqual(machine.reached["debug_script_take"], 2)
        self.assertEqual(rig.audio.underruns(rig.audio.plays[0]), [])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.print_usb_log("script run", machine, marks)
        print("[script run] measure: %s" % measure)

    def test_a_script_for_another_build_is_removed_unrun_and_the_player_still_reconnects(self):
        volume = self.debug_volume(run_txt=b"build 0000000\nplay\nreconnect\n")
        session, machine = self.ejecting_session(volume, usb_answer=None)
        machine.answer_usb_prompt(CONNECT_KEYS, times=2)
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(machine.reached["app_ask_usb_mode"], 1)
        self.assertFalse(session.fat().exists(RUN_PATH))
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        self.assertEqual(report["values"]["script.state"], "refused_build")
        self.assertEqual(machine.reached["app_ask_usb_mode"], 1)
        self.assertEqual([line["status"] for line in report["script"]["lines"]], ["not_run"] * 3)
        self.assertEqual([" ".join(event["detail"].split()[:2]) for event in report["events"]
                          if event["kind"] == "key"], ["play press"])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_a_soak_runs_on_the_battery_with_the_screen_dark_and_reports_its_minute(self):
        session, machine = self.soaking_session(["supply off", "screen off", "soak 1", "reconnect"])
        started_ms = machine.milliseconds()
        machine.run(until_ms=started_ms + 2000)
        self.assertEqual(self.usb_supply_pins(machine), (0, 0))
        self.assertEqual(machine.gpio[2].regs[GPIO_DATA] >> BACKLIGHT_PIN & 1, 0)
        self.assertTrue(machine.cable_bit())
        reached = machine.reached
        self.assertEqual(machine.run(until=lambda: "hal_system_usb_reconnect" in reached,
                                     until_ms=started_ms + SOAK_MINUTE_MS + 5000), "condition met")
        self.assertGreaterEqual(machine.milliseconds(), started_ms + SOAK_MINUTE_MS - 1000)
        self.assertEqual(self.run_until_supply(machine, (1, 1), 1000), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        self.assertEqual([line["status"] for line in report["script"]["lines"]], ["ok"] * 5)
        soak = self.soak_of(report)
        self.assertEqual((soak["line"], soak["minutes"], soak["end"]), (4, 1, "done"))
        self.assertEqual((soak["supply_off_start"], soak["supply_off_end"]), (1, 1))
        self.assertEqual((soak["screen_start"], soak["screen_end"]), (0, 0))
        self.assertEqual((soak["cable_start"], soak["cable_end"]), (1, 1))
        self.assertEqual((soak["audio_start"], soak["audio_end"]), (0, 0))
        self.assertEqual((soak["clock_mhz_start"], soak["clock_mhz_end"]), (24, 24))
        self.assertIn(soak["samples"], (0, 1))
        self.assertIsNone(soak["mv_per_hour"])
        self.assertTrue(0 <= soak["busy_percent"] <= 100)
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        print("\n[soak] %s" % soak)

    def test_a_key_ends_a_soak_and_gives_the_usb_supply_back_at_once(self):
        session, machine = self.soaking_session(["supply off", "soak 5", "reconnect"])
        machine.run(until_ms=machine.milliseconds() + 3000)
        self.assertEqual(self.usb_supply_pins(machine), (0, 0))
        self.assertNotIn("hal_system_usb_reconnect", machine.reached)
        machine.press_key(KEY_PLAY, delay_ms=20, hold_ms=KEY_HOLD_MS)
        self.assertEqual(self.run_until_supply(machine, (1, 1), 1000), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        self.assertEqual([line["status"] for line in report["script"]["lines"]],
                         ["ok", "ok", "cut_short", "ok"])
        soak = self.soak_of(report)
        self.assertEqual((soak["minutes"], soak["end"], soak["supply_off_end"]), (0, "key", 1))
        self.assert_healthy(session)

    def test_a_usb_session_stops_a_soak_and_the_supply_follows_the_cable_again(self):
        session, machine = self.soaking_session(["supply off", "soak 5"])
        machine.run(until_ms=machine.milliseconds() + 2000)
        machine.vbus = False
        machine.run(until_ms=machine.milliseconds() + 500)
        machine.vbus = True
        machine.run(until_ms=machine.milliseconds() + 500)
        self.assertEqual(self.usb_supply_pins(machine), (0, 0))
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        self.assertEqual(self.usb_supply_pins(machine), (1, 1))
        report = self.read_report(session)
        self.assertEqual(report["values"]["script.state"], "aborted")
        self.assertEqual(self.soak_of(report)["end"], "stopped")
        machine.run(until_ms=machine.milliseconds() + QUIET_AFTER_MS)
        self.assertEqual(self.usb_supply_pins(machine), (1, 1))
        self.assertEqual(machine.usb_connect_count, 2)
        self.assert_healthy(session)

    def test_switching_off_during_a_soak_leaves_the_supply_with_the_cable_for_charging(self):
        session, machine = self.soaking_session(["supply off", "soak 5"])
        machine.note("debug_report_write")
        machine.run(until_ms=COLD_REPORT_MS + 500)
        self.assertEqual(self.usb_supply_pins(machine), (0, 0))
        self.hold_power(machine)
        machine.run(until=lambda: machine.reached.get("debug_report_write", 0) >= 1,
                    until_ms=machine.milliseconds() + 8000)
        machine.run(until_ms=machine.milliseconds() + QUIET_AFTER_MS)
        self.assertEqual(self.usb_supply_pins(machine), (1, 1))
        report = self.read_report(session)
        self.assertEqual(report["values"]["report.reason"], "shutdown")
        self.assertEqual(self.soak_of(report)["end"], "key")
        self.assertEqual(machine.usb_connect_count, 1)
        self.assertIsNone(machine.reset_requested)
        self.assert_healthy(session)

    def test_peek_poke_and_gpio_lines_act_on_the_allowed_registers_and_report_what_they_saw(self):
        lines = ["peek 0x500100e8", "poke 0x5001009c 0x00ff00a5", "gpio 2.0 low", "gpio 2.0 read",
                 "gpio 2.0 high", "gpio 0.14 function 1", "gpio 0.14 input"]
        refused = ["peek 0x60080000", "poke 0x03000000 1", "poke 0x500100e9 1", "gpio 3.1 low"]
        session, machine, report = self.run_probe_script(lines + refused)
        script = report["script"]
        self.assertEqual(report["values"]["script.state"], "finished")
        self.assertEqual([line["status"] for line in script["lines"]],
                         ["ok"] * (1 + len(lines)) + ["refused"] * len(refused) + ["ok"])
        probes = script["probes"]
        self.assertEqual([probe["command"] for probe in probes], lines)
        self.assertEqual([probe["line"] for probe in probes], list(range(2, 2 + len(lines))))
        self.assertEqual([(probe["before"], probe["after"]) for probe in probes[:6]],
                         [(2, 2), (0, 0xA5), (0x011, 0x010), (0x010, 0x010), (0x010, 0x011),
                          (0x011, 0x111)])
        self.assertEqual((probes[6]["before"], probes[6]["after"] & ~1), (0x111, 0))
        self.assertEqual(probes[2]["pin_after"], {"function": 0, "output": True, "level": 0})
        self.assertEqual({probe["cable"] for probe in probes}, {1})
        self.assertEqual({probe["charge_pin"] for probe in probes}, {1})
        times = [probe["ms"] for probe in probes]
        self.assertEqual(times, sorted(times))
        self.assertIn(("grf", 0x9C, 0x00FF00A5), machine.register_log)
        port_2 = [entry[4] & 1 for entry in machine.gpio_log if entry[1] == 2 and entry[2] == GPIO_DATA]
        self.assertEqual(port_2[-2:], [0, 1])
        self.assertEqual(machine.gpio[0].regs[4] >> 14 & 1, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        print("\n[probe script]")
        for probe in probes:
            print("  %s" % probe)

    def test_without_a_script_an_ejected_session_changes_nothing_and_does_not_reconnect(self):
        session, machine = self.ejecting_session(self.debug_volume())
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        machine.run(until_ms=machine.milliseconds() + QUIET_AFTER_MS)
        self.assertEqual(machine.usb_connect_count, 1)
        self.assertNotIn("hal_system_usb_reconnect", machine.reached)
        machine.vbus = False
        machine.run(until_ms=machine.milliseconds() + 300)
        writes_before = len(session.card.writes)
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report_sectors = set(session.file_sectors(self.report_path(session)))
        for slot in range(USB_SCREEN_CAPTURES):
            path = "/%s/usbshot%d.565" % (DEBUG_FOLDER, slot)
            if session.fat().exists(path):
                report_sectors.update(session.file_sectors(path))
        for _, lba, count in session.card.writes[writes_before:]:
            self.assertIn(lba, report_sectors)
        report = self.read_report(session)
        self.assertEqual(report["values"]["script.state"], "none")
        self.assertEqual(report["values"]["usb.last_end"], "ejected")
        self.assertEqual(report["script"],
                         {"lines": [], "measures": [], "screenshots": [], "probes": [], "soaks": [],
                          "rtc": []})
        self.assertEqual([name for name in session.fat().list("/" + DEBUG_FOLDER)
                          if not name.startswith("usbshot")], ["report0.txt"])
        self.assertEqual(machine.reached["debug_script_take"], 2)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_with_an_update_and_a_script_the_install_comes_first_and_the_script_waits(self):
        newer = self.newer_image()
        script = ("build %s\nwait 100\nreconnect\n" % self.build_tag()).encode("ascii")
        volume = self.debug_volume(root=[(IDENTITY.update_file, newer)], run_txt=script)
        first, machine = self.ejecting_session(volume)
        machine.stop_at("board_wait_for_power_hold")
        self.assertEqual(machine.run(until_ms=self.session_module.FRAME_LIMIT_MS),
                         "reached board_wait_for_power_hold")
        self.assertEqual(machine.reached["trial_module_install"], 1)
        self.assertNotIn("debug_script_take", machine.reached)
        self.assertNotIn("board_restart", machine.reached)
        self.assertIsNone(machine.reset_requested)
        self.assertEqual(first.fat().read(RUN_PATH), script)
        self.assertFalse(first.fat().exists("/" + IDENTITY.update_file))
        self.assertEqual(first.slot_image("slot_1"), newer[:self.session_module.SLOT_BYTES])
        second = first.reboot(image=first.slot_image("slot_1") + newer[-4:])
        restarted = second.machine
        for name in ("usb_session_storage_changed", "debug_script_take", "hal_system_usb_reconnect",
                     "trial_module_install"):
            restarted.note(name)
        restarted.host_ejects()
        restarted.vbus = True
        self.assertEqual(self.run_usb_sessions(second, 1), "condition met")
        self.assertFalse(second.fat().exists(RUN_PATH))
        self.assertEqual(self.run_until_reconnect_request(restarted), "condition met")
        self.assertEqual(self.run_usb_sessions(second, 2), "condition met")
        report = self.read_report(second)
        self.assertEqual(report["values"]["script.state"], "finished")
        self.assertEqual([line["status"] for line in report["script"]["lines"]], ["ok"] * 3)
        self.assertNotIn("trial_module_install", restarted.reached)
        self.assert_healthy(second)

    def test_a_restart_request_ends_a_successful_install_in_a_restart_into_the_new_image(self):
        newer = self.newer_image()
        volume = self.debug_volume(root=[(IDENTITY.update_file, newer)], restart_req=RESTART_REQUEST)
        first, machine = self.ejecting_session(volume)
        self.assertEqual(machine.run(until_ms=self.session_module.FRAME_LIMIT_MS),
                         "system reset requested")
        self.assertEqual(machine.reached["trial_module_install"], 1)
        self.assertEqual(machine.reached["board_restart"], 1)
        self.assertNotIn("board_wait_for_power_hold", machine.reached)
        self.assertEqual(first.fat().read(RESTART_PATH), RESTART_ANSWER)
        self.assertFalse(first.fat().exists("/" + IDENTITY.update_file))
        self.assertEqual(first.slot_image("slot_1"), newer[:self.session_module.SLOT_BYTES])
        self.assertEqual(first.slot_image("slot_2"), self.image[:self.session_module.SLOT_BYTES])
        self.assertEqual(self.trial_record(first), (TRIAL_RUNNING, 0))
        self.assertEqual([value for _, value in machine.pmu_flag_writes], [BOOT_FLAG_CLEAR])
        self.assertNotIn(LOADER_SELECT_VALUE,
                         [value for _, offset, value in machine.grf_log
                          if offset == LOADER_SELECT_REGISTER])
        self.assertEqual(machine.systick.vtor, 0)
        self.assertEqual(machine.panics, [])
        self.assertEqual(self.read_report(first)["values"]["report.reason"], "restart")
        second = first.reboot(image=first.slot_image("slot_1") + newer[-4:], usb_answer=None)
        restarted = second.machine
        for name in ("usb_session_storage_changed", "trial_module_install", "board_restart",
                     "app_ask_usb_mode"):
            restarted.note(name)
        restarted.host_ejects()
        restarted.vbus = True
        restarted.cable_bit_dead = True
        self.assertEqual(self.run_usb_sessions(second, 1), "condition met")
        report = self.read_report(second)
        values = report["values"]
        self.assertEqual(values["firmware.build"], self.build_tag())
        self.assertEqual(values["usb.last_end"], "none")
        self.assertEqual((values["power.vbus"], values["power.usb_powered"]), (0, 1))
        self.assertEqual((values["loop.boot"], values["loop.connect_attempts"],
                          values["loop.connect_answered"], values["loop.last_connect"],
                          values["loop.cable_bit_at_connect"]),
                         ("loop_restart", 1, 1, "answered", 0))
        restarted.run(until_ms=restarted.milliseconds() + 1000)
        self.assertFalse(second.fat().exists(RESTART_PATH))
        self.assertEqual(restarted.usb_connect_count, 1)
        self.assertNotIn("trial_module_install", restarted.reached)
        self.assertNotIn("board_restart", restarted.reached)
        self.assertNotIn("app_ask_usb_mode", restarted.reached)
        self.assert_healthy(second)
        self.print_usb_log("restart after install", machine)
        self.print_usb_log("after the restart", restarted)

    def test_a_user_mode_request_reconnects_after_the_cable_bit_dies(self):
        script = ("build %s\nset USB mode=Connect to computer\nwait 10000\n"
                  % self.build_tag()).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script),
                                                 notes=("hal_system_request_usb_mode",))
        machine.cable_bit_dies_with_usb = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(machine.run(
            until=lambda: machine.reached.get("hal_system_request_usb_mode", 0) > 0,
            until_ms=machine.milliseconds() + 2000), "condition met")
        self.assertTrue(machine.cable_bit_dead)
        self.assertNotIn("hal_system_usb_reconnect", machine.reached)
        self.assertEqual(machine.run(
            until=lambda: machine.reached.get("usb_session_storage_changed", 0) >= 2,
            until_ms=machine.milliseconds() + 4000), "condition met")
        self.assertEqual(machine.usb_connect_count, 2)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_a_reconnect_works_when_the_cable_bit_has_died_with_the_usb_power(self):
        script = ("build %s\nusb_sense kept_on\nusb_sense normal\nwait 100\nreconnect\n"
                  % self.build_tag()).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        machine.cable_bit_dies_with_usb = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.read_report(session)["values"]["power.vbus"], 1)
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertTrue(machine.cable_bit_dead)
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        values = report["values"]
        self.assertEqual(values["report.reason"], "usb_session")
        self.assertEqual(values["script.state"], "finished")
        self.assertEqual([line["status"] for line in report["script"]["lines"]], ["ok"] * 5)
        self.assertEqual((values["power.vbus"], values["power.usb_powered"]), (0, 1))
        self.assertEqual((values["usb.grf_e8"], values["usb.b_session"]), (0, 1))
        self.assertEqual((values["loop.boot"], values["loop.connect_attempts"],
                          values["loop.connect_answered"], values["loop.connect_unanswered"],
                          values["loop.last_connect"], values["loop.cable_bit_at_connect"]),
                         ("cold", 1, 1, 0, "answered", 0))
        self.assertEqual(values["usb.last_end"], "ejected")
        machine.run(until_ms=machine.milliseconds() + QUIET_AFTER_MS)
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertEqual(machine.usb_log[-1][1], "soft disconnect")
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.print_usb_log("reconnect without the cable bit", machine)

    def test_a_reconnect_nobody_answers_powers_usb_down_and_leaves_a_report(self):
        script = ("build %s\nreconnect\n" % self.build_tag()).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        machine.cable_bit_dies_with_usb = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        machine.vbus = False
        machine.run(until_ms=machine.milliseconds() + RECONNECT_WAIT_MS + RECONNECT_OFF_MS + 2000)
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertEqual(machine.reached["usb_session_storage_changed"], 1)
        self.assertEqual(machine.usb_log[-1][1], "soft disconnect")
        values = self.read_report(session)["values"]
        self.assertEqual((values["report.reason"], values["loop.connect_attempts"],
                          values["loop.connect_answered"], values["loop.connect_unanswered"],
                          values["loop.last_connect"]), ("no_host", 1, 0, 1, "no_host"))
        self.assertEqual(values["script.state"], "finished")
        self.assertEqual(values["power.usb_powered"], 0)
        machine.run(until_ms=machine.milliseconds() + QUIET_AFTER_MS)
        self.assertEqual(machine.usb_connect_count, 2)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_a_restart_request_does_nothing_after_a_refused_or_failed_install(self):
        refused = self.session_module.with_brand(self.image, "EchoPod")
        for image, fail in ((refused, False), (self.newer_image(), True)):
            volume = self.debug_volume(root=[(IDENTITY.update_file, image)],
                                       restart_req=RESTART_REQUEST)
            session, machine = self.ejecting_session(volume)
            if fail:
                machine.stub_symbol("trial_module_install", lambda machine: 0)
            self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
            machine.run(until_ms=machine.milliseconds() + 1000)
            self.assertIsNone(machine.reset_requested)
            self.assertNotIn("board_restart", machine.reached)
            self.assertNotIn("debug_restart_after_install", machine.reached)
            self.assertEqual("trial_module_install" in machine.reached, fail)
            self.assertFalse(session.fat().exists(RESTART_PATH))
            self.assertEqual(session.fat().exists("/" + IDENTITY.update_file), fail)
            self.assertEqual(session.slot_image("slot_1"), self.image[:self.session_module.SLOT_BYTES])
            self.assertEqual(machine.panics, [])


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
