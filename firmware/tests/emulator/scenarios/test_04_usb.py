import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    CABLE_NOTICED_LIMIT_MS,
    CABLE_STEADY_MS,
    CHARGER_MS,
    CLOCK_SLACK_MS,
    CLOCK_TICK_MS,
    CODE_WINDOW_BYTES,
    GRF_WORDS,
    GRF_WORDS_POWERED,
    IDENTITY,
    KEY_HOLD_MS,
    KEY_SETTLE_MS,
    PROFILED_MS,
    PROFILE_BUCKET_BYTES,
    REPORT_START_LIMIT_MS,
    USB_PHY_ON,
    USB_PHY_TUNING,
    USB_POWER_DOWN,
    USB_POWER_UP,
    echo_debug,
    make_image,
    ACTIVITY_WRITING,
    COLUMN_BANDS_LIMIT,
    LEAVE_FORCE_HOLD_MS,
    LEAVE_GRACE_MS,
    LEAVE_HOLD_MS,
    STORAGE_MOTION_FRAME_MS,
    TRAFFIC_MEASURE_MS,
)


class EchoImageTest(EchoImageHarness):
    def test_the_usb_session_and_the_update_check_run_from_the_usb_overlay(self):
        from unicorn.arm_const import UC_ARM_REG_R0
        session = self.session(files=[(IDENTITY.update_file, self.image, None)], vbus=True)
        machine = session.machine
        renderers = []
        machine.hook_symbol("app_set_usb_renderer",
                            lambda: renderers.append(machine.reg(UC_ARM_REG_R0)))
        self.assertIn("usb_screen_draw", machine.overlays[0].symbols)
        self.assertNotIn("usb_screen_draw", machine.symbols)
        machine.note("usb_screen_draw")
        machine.note("updater_check")
        machine.note("usb_storage_session")
        session.run_to_first_frame()
        self.assertFalse(machine.overlays[0].loaded(machine))
        self.assertFalse(any(renderers))
        self.assertEqual(session.run_through_update_check(),
                         "reached usb_session_storage_changed")
        self.assertTrue(machine.overlays[0].loaded(machine))
        window = range(make_image.OVERLAY_BASE, make_image.OVERLAY_BASE + make_image.OVERLAY_CAPACITY)
        self.assertIn(machine.current_pc(), window)
        self.assertEqual(machine.reached["usb_storage_session"], 1)
        self.assertEqual(machine.reached["updater_check"], 1)
        self.assertGreater(machine.reached.get("usb_screen_draw", 0), 0)
        self.assertTrue(session.usb_overlay_reads("slot_1"))
        self.assertEqual(session.usb_overlay_reads("slot_2"), [])
        self.assertEqual(renderers[-1] & ~1, machine.overlays[0].symbols["usb_screen_draw"])
        machine.continue_past_stop()
        self.assertEqual(machine.run(until=lambda: renderers[-1] == 0,
                                     until_ms=machine.milliseconds() + 2000), "condition met")
        self.assert_healthy(session)

    def test_a_usb_session_starts_by_writing_a_debug_report(self):
        session = self.session(vbus=True)
        machine = session.machine
        marks = {}
        for name in ("usb_storage_session", "debug_report_write", "usb_silence_audio"):
            self.mark(machine, marks, name)
        self.mark(machine, marks, "sd_start", after="debug_report_write")
        self.assertEqual(self.boot_until_waiting_for_key(session),
                         "reached usb_session_storage_changed")
        started, served = marks["usb_storage_session"], marks["usb_silence_audio"]
        report_ms = (marks["sd_start"][1] - marks["debug_report_write"][1]) / 1000
        report = self.read_report(session)
        values = report["values"]
        self.assertFalse(report["truncated"])
        self.assertEqual(report["unread_lines"], [])
        self.assertEqual((values["report.sequence"], values["report.reason"],
                          values["report.cold_start"]), (1, "usb_session", 1))
        self.assertEqual(values["firmware.name"], IDENTITY.name)
        self.assertEqual(values["firmware.version"], IDENTITY.version)
        self.assertEqual(values["firmware.build"],
                         self.image[0x30:0x38].rstrip(b"\0").decode("ascii"))
        self.assertTrue(started[1] // 1000 - CLOCK_SLACK_MS <= values["uptime_ms"]
                        <= served[1] // 1000 + CLOCK_SLACK_MS)
        self.assertIn(values["clock_mhz"], (96, 200))
        self.assertTrue(values["core0.idle_us"] <= values["core0.clock_us"])
        self.assertTrue(3900 <= values["battery.millivolts"] <= 4100)
        self.assertEqual(values["battery.usb_power"], 1)
        self.assertEqual((values["power.vbus"], values["power.usb_powered"],
                          values["power.charge_pin"]), (1, 1, 1))
        self.assertEqual([values["usb.grf_%s_start" % word] for word in GRF_WORDS], [0, 0, 0, 0, 0])
        self.assertEqual([values["usb.grf_%s" % word] for word in GRF_WORDS], GRF_WORDS_POWERED)
        self.assertEqual((values["usb.gotgctl"], values["usb.b_session"]), (0x00090000, 1))
        self.assertEqual((values["power.adc_raw"], values["power.adc_millivolts"]), (819, 3999))
        self.assertEqual(values["power.gauge_millivolts"], 3999)
        self.assertEqual(values["power.percent_shown"], values["battery.percent"])
        self.assertEqual((values["power.capped"], values["power.held"]), (0, 0))
        self.assertEqual((values["start.count"], values["start.warm"]), (1, 0))
        self.assertIsNone(values["start.previous_uptime_ms"])
        self.assertIsNone(values["fault.pc"])
        self.assertEqual(values["usb.last_end"], "none")
        self.assertIsNone(values["usb.internal_sectors_written"])
        self.assertEqual((values["audio.state"], values["audio.running"]), ("stopped", 0))
        self.assertIsNone(values["audio.underruns"])
        self.assertEqual(values["frame.count"] == 0, values["frame.total_avg_us"] is None)
        kinds = [event["kind"] for event in report["events"]]
        self.assertEqual(kinds[0], "screen")
        self.assertEqual(kinds[-1], "usb_start")
        self.assertEqual(values["events.recorded"], len(kinds))
        self.assertEqual(values["profile.state"], "off")
        self.assertEqual(values["profile.samples"], 0)
        self.assertEqual(values["profile.code_base"], make_image.CODE_BASE)
        self.assertEqual(values["profile.bucket_bytes"], PROFILE_BUCKET_BYTES)
        self.assertEqual(values["profile.buckets"], -(-CODE_WINDOW_BYTES // PROFILE_BUCKET_BYTES))
        self.assertEqual(report["profile_buckets"], {})
        sectors = session.file_sectors(self.report_path(session))
        stamps = [stamp for stamp, lba, count in session.card.writes if lba in sectors]
        self.assertTrue(stamps)
        self.assertTrue(all(started[0] < stamp < served[0] for stamp in stamps))
        delay_ms = (served[1] - started[1]) / 1000
        self.assertLess(delay_ms, REPORT_START_LIMIT_MS)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        text_bytes = len(session.fat().read(self.report_path(session)).rstrip(b"\n"))
        print("\n[debug report] %d bytes of text, written in %.1f ms; %.1f ms from the session's "
              "start to serving the host" % (text_bytes, report_ms, delay_ms))

    def test_a_second_session_reports_the_first_and_reuses_the_report_file(self):
        session = self.session(vbus=True)
        machine = session.machine
        machine.note("usb_session_storage_changed")
        machine.answer_waits_for_key(4)
        session.run_to_first_frame()
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        first_sectors = session.file_sectors(self.report_path(session))
        machine.run(until_ms=machine.milliseconds() + 1500)
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        values = report["values"]
        self.assertGreaterEqual(values["frame.count"], 1)
        self.assertGreater(values["frame.total_avg_us"], 0)
        self.assertEqual(values["usb.last_end"], "unplugged")
        self.assertEqual(values["usb.left_mounted"], 1)
        self.assertEqual(values["usb.internal_sectors_written"], 0)
        kinds = [event["kind"] for event in report["events"]]
        self.assertEqual(kinds.count("usb_start"), 2)
        self.assertEqual(kinds.count("usb_end"), 1)
        powers = [event["detail"] for event in report["events"] if event["kind"] == "power"]
        self.assertEqual(powers, ["1 1", "0 1", "1 1"])
        self.assertEqual(values["power.samples"], 1)
        sample = report["power_samples"][0]
        self.assertEqual((sample["vbus"], sample["charge_pin"], sample["playing"],
                          sample["usb_powered"]), (True, True, False, True))
        self.assertEqual((sample["minute"], sample["millivolts"]), (0, 3999))
        self.assertEqual(sample["percent"], values["battery.percent"])
        self.assertEqual((values["report.sequence"], values["report.cold_start"]), (2, 1))
        self.assertEqual(session.file_sectors(self.report_path(session)), first_sectors)
        self.assertEqual(session.fat().list("/" + IDENTITY.data_directory + "/debug"),
                         ["report0.txt"])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_a_cable_must_hold_for_a_second_before_usb_is_powered_up(self):
        session = self.session()
        machine = session.machine
        listens = machine.trace("usb_session_listen")
        session.run_to_home_screen()
        machine.run(until_ms=machine.milliseconds() + 300)
        machine.vbus = True
        machine.run(until_ms=machine.milliseconds() + 700)
        machine.vbus = False
        machine.run(until_ms=machine.milliseconds() + 100)
        self.assertEqual(listens, [])
        self.assertEqual(machine.usb_log, [])
        machine.vbus = True
        plugged_ms = machine.milliseconds()
        machine.run(until=lambda: listens, until_ms=plugged_ms + 3000)
        self.assertEqual(len(listens), 1)
        self.assertGreaterEqual(listens[0], plugged_ms + CABLE_STEADY_MS - CLOCK_TICK_MS)
        self.assertLess(listens[0], plugged_ms + CABLE_STEADY_MS + CABLE_NOTICED_LIMIT_MS)
        self.assert_healthy(session)

    def test_on_a_charger_usb_keeps_listening_until_a_host_appears_or_the_cable_leaves(self):
        session = self.session()
        machine = session.machine
        for name in ("usb_session_storage_changed", "usb_power_down"):
            machine.note(name)
        machine.answer_waits_for_key(4)
        machine.host_absent = True
        session.run_to_home_screen()
        machine.vbus = True
        machine.run(until_ms=machine.milliseconds() + CHARGER_MS)
        self.assertEqual([event for _, event in machine.usb_log], ["soft disconnect", "pull-up on"])
        self.assertNotIn("usb_power_down", machine.reached)
        machine.vbus = False
        machine.run(until=lambda: "usb_power_down" in machine.reached,
                    until_ms=machine.milliseconds() + 1000)
        self.assertEqual(machine.reached["usb_power_down"], 1)
        self.assertEqual(machine.usb_log[-1][1], "soft disconnect")
        machine.vbus = True
        machine.run(until_ms=machine.milliseconds() + CHARGER_MS)
        self.assertEqual(machine.usb_connect_count, 2)
        self.assertEqual(machine.reached["usb_power_down"], 1)
        self.assertNotIn("usb_session_storage_changed", machine.reached)
        machine.host_arrives()
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(machine.usb_connect_count, 2)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        self.print_usb_log("charger, then a host", machine)

    def test_usb_power_up_writes_the_words_stock_writes(self):
        session = self.session(vbus=True)
        machine = session.machine
        self.assertEqual(self.boot_until_waiting_for_key(session),
                         "reached usb_session_storage_changed")
        log = [entry for entry in machine.register_log if entry[0] in ("grf", "cru")]
        up = self.index_of(log, USB_POWER_UP + USB_PHY_ON + USB_PHY_TUNING)
        self.assertIsNotNone(up)
        self.assertIsNotNone(self.index_of(log, USB_POWER_DOWN, up))
        self.assert_healthy(session)

    def test_usb_sense_debug_commands_restore_automatic_power(self):
        session, machine, report = self.run_probe_script(
            ["usb_sense kept_on", "wait 100", "usb_sense normal"])
        self.assertEqual(report["values"]["script.state"], "finished")
        self.assertEqual([line["status"] for line in report["script"]["lines"]], ["ok"] * 5)
        self.assertNotEqual(report["values"]["usb.sense_mode"], "kept_on")
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_the_profiler_row_fills_a_histogram_the_host_tool_symbolises(self):
        session = self.session()
        machine = session.machine
        machine.stub_symbol("extension_home_art_draw_backdrop", lambda machine: None)
        self.assertEqual(make_image.OVERLAY_BASE, machine.symbols["device_profile_overlay_base"])
        self.assertEqual(make_image.EFFECTS_MODULE_BASE + make_image.EFFECTS_MODULE_CAPACITY,
                         machine.symbols["device_profile_overlay_base"] +
                         machine.symbols["device_profile_overlay_bytes"])
        machine.note("hal_system_return_scratch")
        machine.note("usb_session_storage_changed")
        machine.answer_waits_for_key(4)
        machine.run(until=lambda: "hal_system_return_scratch" in machine.reached,
                    until_ms=self.session_module.FRAME_LIMIT_MS)
        machine.run(until_ms=machine.milliseconds() + 1500)
        self.switch_profiler_on(machine)
        switched_ms = machine.milliseconds()
        machine.run(until_ms=switched_ms + PROFILED_MS)
        marks = {}
        self.mark(machine, marks, "usb_silence_audio")
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        report = self.read_report(session)
        values = report["values"]
        self.assertEqual(values["profile.state"], "on")
        served_ms = marks["usb_silence_audio"][1] // 1000
        self.assertTrue(PROFILED_MS <= values["profile.samples"]
                        <= served_ms - switched_ms + KEY_SETTLE_MS + KEY_HOLD_MS)
        self.assertTrue(abs(values["uptime_ms"] - served_ms) <= REPORT_START_LIMIT_MS)
        nm_text = echo_debug.run_nm(self.elf)
        profile = echo_debug.symbolise(report, nm_text, top=100000)
        self.assertTrue(profile["consistent"])
        shares = profile["shares_ms"]
        self.assertGreater(shares["code"], PROFILED_MS // 10)
        self.assertGreater(shares["idle"], PROFILED_MS // 10)
        self.assertEqual(shares["other"], 0)
        attributed = sum(row["ms"] for row in profile["functions"])
        self.assertTrue(abs(attributed - profile["shares_ms"]["code"]) <= 1)
        self.assertGreater(len(profile["functions"]), 5)
        self.assertNotIn(echo_debug.UNATTRIBUTED, [row["name"] for row in profile["functions"][:3]])
        self.assertTrue(any(row["name"].endswith(".c") for row in profile["files"]))
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        print("\n[profile] %d samples, %s; top functions: %s" % (
            profile["samples"], shares, ", ".join("%s %.0f ms" % (row["name"], row["ms"])
                                          for row in profile["functions"][:5])))


    def test_holding_back_on_a_quiet_host_removes_the_media_and_ends_like_an_eject(self):
        session, machine, _, applied, removed = self.staying_storage_session(quiet=True)
        pressed = self.hold_back(machine, LEAVE_HOLD_MS)
        self.assertEqual(self.session_ended(machine), "condition met")
        ended = machine.milliseconds()
        self.assertEqual(len(removed), 1)
        self.assertGreaterEqual(removed[0] - pressed, 1000 - CLOCK_SLACK_MS)
        self.assertGreaterEqual(ended - removed[0], LEAVE_GRACE_MS - CLOCK_SLACK_MS)
        self.assertEqual(applied, [1])
        self.assertNotIn("board_wait_for_key", machine.reached)
        self.assert_healthy(session)
        print("\n[player-side disconnect] media removed %d ms after Back, session ended %d ms "
              "later" % (removed[0] - pressed, ended - removed[0]))

    def test_a_disconnect_while_the_computer_writes_waits_for_it_to_finish(self):
        session, machine, state, applied, removed = self.staying_storage_session(quiet=False)
        self.hold_back(machine, LEAVE_HOLD_MS)
        machine.run(until_ms=machine.milliseconds() + 3000)
        self.assertEqual(removed, [])
        self.assertNotIn("usb_session_storage_changed", machine.reached)
        quiet_at = machine.milliseconds()
        state["quiet"] = True
        self.assertEqual(self.session_ended(machine), "condition met")
        self.assertEqual(len(removed), 1)
        self.assertLess(removed[0] - quiet_at, 50)
        self.assertEqual(applied, [1])
        self.assertNotIn("board_wait_for_key", machine.reached)
        self.assert_healthy(session)

    def test_keeping_back_held_while_the_computer_writes_ends_like_an_unplug(self):
        session, machine, _, applied, removed = self.staying_storage_session(quiet=False)
        machine.answer_waits_for_key(4)
        pressed = self.hold_back(machine, LEAVE_FORCE_HOLD_MS)
        self.assertEqual(self.session_ended(machine), "condition met")
        self.assertEqual(removed, [])
        self.assertEqual(applied, [0])
        self.assertIn("board_wait_for_key", machine.reached)
        self.assertGreaterEqual(machine.milliseconds() - pressed, 4000 - CLOCK_SLACK_MS)
        self.assert_healthy(session)

    def test_storage_motion_leaves_the_usb_loop_room_while_the_host_writes(self):
        session, machine, _, _, _ = self.staying_storage_session(quiet=True)
        if "usb_activity_now" not in machine.overlays[0].symbols:
            rate, gap = self.poll_rate(machine, TRAFFIC_MEASURE_MS)
            print("\n[storage before] usb_poll %.0f/s, longest gap %.2f ms" % (rate, gap / 1000))
            return
        traffic = {"kind": 0, "kib": 0}
        machine.stub_symbol("usb_activity_now", lambda machine: traffic["kind"])

        def copied(machine):
            traffic["kib"] += 64 if traffic["kind"] else 0
            return traffic["kib"]

        machine.stub_symbol("usb_activity_written_kib", copied)
        areas = machine.trace("app_show_usb_area")
        polls = self.poll_stamps(machine)
        still_rate, still_gap = self.poll_rate(machine, TRAFFIC_MEASURE_MS, polls)
        self.assertEqual(areas, [])
        traffic["kind"] = ACTIVITY_WRITING
        machine.run(until_ms=machine.milliseconds() + 300)
        moving_rate, moving_gap = self.poll_rate(machine, TRAFFIC_MEASURE_MS, polls)
        frames = len(areas)
        self.assertGreaterEqual(frames, TRAFFIC_MEASURE_MS // STORAGE_MOTION_FRAME_MS)
        self.assertLessEqual(frames, TRAFFIC_MEASURE_MS // STORAGE_MOTION_FRAME_MS
                             + (TRAFFIC_MEASURE_MS // 1000 + 1) * COLUMN_BANDS_LIMIT)
        self.assert_healthy(session)
        print("\n[storage motion] usb_poll %.0f/s still, %.0f/s with motion (%.1f %% fewer); "
              "longest gap %.2f ms still, %.2f ms with motion; %d area frames"
              % (still_rate, moving_rate, 100 * (1 - moving_rate / still_rate), still_gap / 1000,
                 moving_gap / 1000, frames))


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
