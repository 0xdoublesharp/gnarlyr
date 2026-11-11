import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import echo_image_harness as image_tests

from echo_image_harness import (
    EchoImageHarness,
    main,
    HEIGHT,
    HOME_MIXTAPES_BEFORE_SETTINGS,
    KEY_BACK,
    KEY_NEXT,
    KEY_PLAY,
    LANGUAGE_RAM,
    MODULE_LOAD_LIMIT_MS,
    MODULE_REUSE_LIMIT_US,
    MUSIC_ROWS_BETWEEN_SONGS_AND_COVER_FLOW,
    REPEAT_ALL,
    SETTINGS_PATH,
    SETTINGS_REPEAT_FIELD,
    SETTINGS_REPEAT_ROW,
    STEP_GAP_WITH_A_MODULE_MS,
    WIDTH,
    english,
    make_image,
    tempfile,
)


class EchoImageTest(EchoImageHarness):
    def test_settings_load_from_slot_1_when_first_opened_and_are_reused_from_the_slot(self):
        from echo_emulator.modules import ModuleWatch
        session, machine = self.started_session()
        watch = ModuleWatch(session)
        number, settings = self.module(machine, "settings")
        resident_ms = self.release_to_first_draw_ms(machine, KEY_PLAY)
        self.press(machine, KEY_BACK)
        self.assertEqual(watch.loads(), [])
        self.press(machine, image_tests.KEY_PREVIOUS)
        module_ms = self.release_to_first_draw_ms(machine, KEY_PLAY)
        self.assertLessEqual(module_ms - resident_ms, MODULE_LOAD_LIMIT_MS)
        loads = watch.loads()
        self.assertEqual([(call["module"], call["loaded"]) for call in loads], [(number, True)])
        self.assertLess(loads[0]["us"], MODULE_LOAD_LIMIT_MS * 1000)
        self.assertTrue(session.module_reads("slot_1", number))
        self.assertEqual(session.module_reads("slot_2", number), [])
        self.assertTrue(settings.loaded(machine))
        for key in (KEY_PLAY, KEY_BACK, KEY_BACK, KEY_PLAY, image_tests.KEY_PREVIOUS, KEY_PLAY, KEY_BACK):
            self.press(machine, key)
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()],
                         [(number, True), (1, True), (number, True)])
        self.assertLess(max(call["us"] for call in watch.calls if not call["sectors"]),
                        MODULE_REUSE_LIMIT_US)
        self.assertTrue(settings.loaded(machine))
        self.assert_module_code_never_written(watch)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        print("\n[module load after a rescan] " + watch.describe(loads[0]))
        restarted = session.reboot()
        again = ModuleWatch(restarted)
        restarted.run_to_home_screen()
        restarted.machine.run(until_ms=restarted.machine.milliseconds() + 1500)
        self.open_settings(restarted.machine)
        cold = again.loads()
        self.assertEqual([(call["module"], call["loaded"]) for call in cold], [(number, True)])
        self.assertLess(cold[0]["us"], MODULE_LOAD_LIMIT_MS * 1000)
        self.assert_healthy(restarted)
        print("[first module load after a start, code CRC included] " + again.describe(cold[0]))
        print("[key release to first pixels] Music, resident: %d ms; Settings, loaded: %d ms"
              % (resident_ms, module_ms))

    def test_a_damaged_module_in_slot_1_is_replaced_by_the_copy_in_slot_2(self):
        from echo_emulator.modules import ModuleWatch
        session, machine = self.started_session()
        number, settings = self.module(machine, "settings")
        session.change_module("slot_1", number, 600, lambda byte: byte ^ 0xFF)
        watch = ModuleWatch(session)
        toasts = self.collect_toasts(machine)
        self.open_settings(machine)
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()],
                         [(number, True)])
        self.assertTrue(session.module_reads("slot_1", number))
        self.assertTrue(session.module_reads("slot_2", number))
        self.assertTrue(settings.loaded(machine))
        self.assertEqual(toasts, [])
        self.press(machine, KEY_PLAY)
        self.assertLess(watch.loads()[0]["us"], MODULE_LOAD_LIMIT_MS * 1000)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        print("\n[module load from slot 2] " + watch.describe(watch.loads()[0]))

    def test_without_an_intact_copy_a_module_screen_stays_shut_and_none_of_it_runs(self):
        from echo_emulator.modules import ModuleWatch
        session, machine = self.started_session()
        number, _ = self.module(machine, "settings")
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, number, 600, lambda byte: byte ^ 0xFF)
        watch = ModuleWatch(session)
        watch.count_slot_code()
        toasts = self.collect_toasts(machine)
        self.open_settings(machine)
        self.assert_module_screen_refused(session, watch, toasts, number)
        self.press(machine, KEY_PLAY)
        self.assertEqual(len(toasts), 2)
        self.assertEqual(watch.executed, 0)
        self.assert_healthy(session)

    def test_a_module_built_for_other_code_is_refused_in_slot_1_and_in_both_slots(self):
        from echo_emulator.modules import ModuleWatch
        code_crc_at = 12
        session, machine = self.started_session()
        number, settings = self.module(machine, "settings")
        session.change_module("slot_1", number, code_crc_at, lambda byte: byte ^ 1)
        watch = ModuleWatch(session)
        self.open_settings(machine)
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()],
                         [(number, True)])
        self.assertEqual(session.module_reads("slot_1", number),
                         [(session.module_span("slot_1", number)[0], 1)])
        self.assertTrue(settings.loaded(machine))
        self.assert_healthy(session)
        other, other_machine = self.started_session()
        for slot in ("slot_1", "slot_2"):
            other.change_module(slot, number, code_crc_at, lambda byte: byte ^ 1)
        refused = ModuleWatch(other)
        refused.count_slot_code()
        toasts = self.collect_toasts(other_machine)
        self.open_settings(other_machine)
        self.assert_module_screen_refused(other, refused, toasts, number)
        for slot in ("slot_1", "slot_2"):
            self.assertEqual(other.module_reads(slot, number),
                             [(other.module_span(slot, number)[0], 1)])

    def test_a_usb_session_takes_the_slot_and_the_screen_under_it_gets_its_module_back(self):
        from echo_emulator.modules import ModuleWatch
        session, machine = self.started_session()
        watch = ModuleWatch(session)
        number, settings = self.module(machine, "settings")
        machine.note("usb_session_storage_changed")
        machine.note("usb_storage_session")
        self.open_settings(machine)
        for _ in range(SETTINGS_REPEAT_ROW):
            self.press(machine, KEY_NEXT)
        for key in (KEY_PLAY, KEY_NEXT):
            self.press(machine, key)
        self.assertTrue(settings.loaded(machine))
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(machine.reached["usb_storage_session"], 1)
        self.assertFalse(settings.loaded(machine))
        machine.run(until_ms=machine.milliseconds() + 600)
        self.assertTrue(settings.loaded(machine))
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()],
                         [(number, True)] * 2)
        for key in (KEY_NEXT, KEY_BACK, KEY_NEXT, KEY_PLAY, KEY_BACK, KEY_BACK):
            self.press(machine, key)
        self.assertEqual(len(watch.loads()), 2)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_a_usb_session_restores_about_from_views_then_back_restores_settings(self):
        from echo_emulator.modules import ModuleWatch
        session, machine = self.started_session()
        watch = ModuleWatch(session)
        settings_number, settings = self.module(machine, "settings")
        views_number, views = self.module(machine, "views")
        machine.note("usb_session_storage_changed")
        machine.note("usb_storage_session")
        self.open_settings(machine)
        self.press(machine, KEY_PLAY)
        self.press(machine, KEY_NEXT)
        self.assertTrue(views.loaded(machine))
        self.assertFalse(settings.loaded(machine))
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(machine.reached["usb_storage_session"], 1)
        self.assertFalse(views.loaded(machine))
        machine.run(until_ms=machine.milliseconds() + 600)
        self.assertTrue(views.loaded(machine))
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()],
                         [(settings_number, True), (views_number, True), (views_number, True)])
        self.press(machine, KEY_BACK)
        self.assertTrue(settings.loaded(machine))
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()],
                         [(settings_number, True), (views_number, True), (views_number, True),
                          (settings_number, True)])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_modules_change_places_and_a_setting_is_saved_while_a_track_keeps_playing(self):
        from echo_emulator.modules import ModuleWatch
        rig = self.playback_rig()
        machine, session = rig.machine, rig.session
        watch = ModuleWatch(session)
        settings_number, settings = self.module(machine, "settings")
        views_number, views = self.module(machine, "views")
        pack = make_image.language_packs_in(self.image)[0]
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(1500)
        audible = rig.first_audio_after(mark)
        self.assertIsNotNone(audible)
        rig.press_many("back", 3)
        rig.press("previous")
        opened = rig.press("play")
        self.assertTrue(settings.loaded(machine))
        rig.press_many("next", SETTINGS_REPEAT_ROW)
        rig.press("play")
        rig.press_many("next", REPEAT_ALL)
        rig.press("play")
        self.assertEqual(session.fat().read(SETTINGS_PATH)[SETTINGS_REPEAT_FIELD], REPEAT_ALL)
        rig.press("back")
        rig.press_many("previous", HOME_MIXTAPES_BEFORE_SETTINGS)
        rig.press("play")
        rig.run_for(400)
        self.assertTrue(views.loaded(machine))
        rig.press("back")
        rig.press_many("next", HOME_MIXTAPES_BEFORE_SETTINGS)
        rig.press("play")
        rig.run_for(400)
        self.assertTrue(settings.loaded(machine))
        loads = watch.loads()
        self.assertEqual([(call["module"], call["loaded"]) for call in loads],
                         [(settings_number, True), (views_number, True), (settings_number, True)])
        self.assertLess(max(call["us"] for call in loads), MODULE_LOAD_LIMIT_MS * 1000)
        self.assert_module_code_never_written(watch)
        self.assertEqual(machine.read_memory(LANGUAGE_RAM, len(pack)), pack)
        self.assertEqual(rig.audio.underruns(audible), [])
        self.assertLess(rig.longest_gap(opened), STEP_GAP_WITH_A_MODULE_MS)
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        for call in loads:
            print("\n[module change, track playing] " + watch.describe(call), end="")
        print("\n[main loop] longest gap %d ms" % rig.longest_gap(opened))

    def test_a_script_drives_module_screens_takes_their_pictures_and_changes_a_setting(self):
        from echo_emulator.modules import ModuleWatch
        from echo_emulator.playback import ALBUM_COUNT
        from echo_emulator.real_volume import real_volume
        script = "\n".join(["build " + self.build_tag(), "play", "down,play", "wait 600",
                            "screenshot coverflow", "set Repeat=All", "down", "wait 300",
                            "back:long", "wait 600", "up", "play", "wait 600",
                            "screenshot settings", "measure 500", "reconnect", ""])
        volume = self.debug_volume(real_volume(playable=True, album_count=ALBUM_COUNT),
                                   run_txt=script.encode("ascii"))
        session, machine = self.ejecting_session(volume)
        watch = ModuleWatch(session)
        settings_number, settings = self.module(machine, "settings")
        views_number, _ = self.module(machine, "views")
        library_number, _ = self.module(machine, "library")
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertTrue(settings.loaded(machine))
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        ran = report["script"]
        self.assertEqual(report["values"]["script.state"], "finished")
        self.assertEqual([line["status"] for line in ran["lines"]], ["ok"] * 16)
        self.assertEqual(ran["screenshots"], [{"slot": 0, "name": "coverflow"},
                                              {"slot": 1, "name": "settings"}])
        loads = watch.loads()
        self.assertEqual([(call["module"], call["loaded"]) for call in loads],
                         [(library_number, True), (library_number, True), (views_number, True),
                          (settings_number, True), (views_number, True), (settings_number, True)])
        self.assertLess(max(call["us"] for call in loads), MODULE_LOAD_LIMIT_MS * 1000)
        screens = [event["detail"] for event in report["events"] if event["kind"] == "screen"]
        self.assertIn("3 " + english("STR_COVER_FLOW")[:7], screens)
        self.assertIn("2 " + english("STR_SETTINGS")[:7], screens)
        self.assertEqual(session.fat().read(SETTINGS_PATH)[SETTINGS_REPEAT_FIELD], REPEAT_ALL)
        with tempfile.TemporaryDirectory() as directory:
            shots = self.converted_screenshots(session, directory)
            self.assertEqual([(shot["name"], shot["width"], shot["height"]) for shot in shots],
                             [("coverflow", WIDTH, HEIGHT), ("settings", WIDTH, HEIGHT)])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        print("\n[script on module screens] measure: %s" % ran["measures"][0])

    def test_cover_flow_runs_from_its_module_while_a_track_keeps_playing(self):
        from echo_emulator.modules import ModuleWatch
        rig = self.playback_rig()
        machine, session = rig.machine, rig.session
        watch = ModuleWatch(session)
        number, views = self.module(machine, "views")
        pack = make_image.language_packs_in(self.image)[0]
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(1500)
        audible = rig.first_audio_after(mark)
        self.assertIsNotNone(audible)
        self.assertEqual(watch.loads(), [])
        resident_writes = watch.other_writes
        rig.run_for(1500)
        rig.press("back")
        rig.press("back")
        rig.press_many("previous", MUSIC_ROWS_BETWEEN_SONGS_AND_COVER_FLOW)
        self.assertEqual(watch.other_writes, resident_writes)
        opened = rig.press("play")
        rig.run_for(800)
        first = watch.loads()
        self.assertEqual([(call["module"], call["loaded"]) for call in first], [(number, True)])
        self.assertLess(first[0]["us"], MODULE_LOAD_LIMIT_MS * 1000)
        self.assertTrue(session.module_reads("slot_1", number))
        self.assertEqual(session.module_reads("slot_2", number), [])
        self.assertTrue(views.loaded(machine))
        rig.press_many("next", 3)
        rig.run_for(600)
        rig.press("back")
        rig.press("play")
        rig.run_for(600)
        self.assertEqual(watch.loads(), first)
        reuses = [call["us"] for call in watch.calls if not call["sectors"]]
        self.assertTrue(reuses)
        self.assertLess(max(reuses), MODULE_REUSE_LIMIT_US)
        self.assertTrue(views.loaded(machine))
        self.assert_module_code_never_written(watch)
        self.assertEqual(machine.read_memory(LANGUAGE_RAM, len(pack)), pack)
        self.assertEqual(rig.audio.underruns(audible), [])
        self.assertLess(rig.longest_gap(opened), STEP_GAP_WITH_A_MODULE_MS)
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_healthy(session)
        print("\n[module load, track playing] " + watch.describe(first[0]))
        print("[module reuse] %d calls, longest %d us" % (len(reuses), max(reuses)))

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



if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
