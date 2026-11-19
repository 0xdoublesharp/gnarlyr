import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from echo_image_harness import (
    EchoImageHarness,
    IDENTITY,
    KEY_BACK,
    KEY_MENU,
    KEY_PLAY,
    MODULE_LOAD_LIMIT_MS,
    MUSIC_ROWS_BETWEEN_SONGS_AND_COVER_FLOW,
    SETTINGS_PATH,
    STEP_GAP_WITH_A_MODULE_MS,
    english,
    fnv1a,
    main,
    struct,
)
from test_22_meter_styles import MovingFlac

COVER, CASSETTE, VU, SPECTRUM = 0, 1, 2, 3
QUICK_SETTINGS_STYLE_ROW_FROM_END = 3

ANIMATE_MS = 600
STRIP_ALBUMS = 3
BROWSE_SCRIPT = ["set Now Playing style=VU meters", "play", "down,down,play", "wait 600", "play",
                 "wait 900", "play", "wait 700", "back", "wait 700", "back", "wait 700", "back",
                 "wait 400", "down,down,play", "wait 600", "play", "wait 700",
                 "set Now Playing style=Cassette", "wait 300", "play", "wait 900", "menu",
                 "wait 900", "screenshot cassette", "set Now Playing style=VU meters", "wait 900",
                 "screenshot vu", "reconnect"]


class EchoImageTest(EchoImageHarness):
    def styled_volume(self, style):
        from echo_emulator.fat_volume import Fat32Volume
        settings = bytearray(self.saved_settings(4, 15, shuffle=0))
        settings[7] = 6
        settings[51] = (style & 1) << 5
        settings[52] = (style >> 1) << 7
        settings[54:58] = struct.pack("<I", fnv1a(settings[:54]))
        volume = Fat32Volume()
        volume.add_file("Moving.flac", source=MovingFlac())
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("settings.bin", settings, directory=IDENTITY.data_directory)
        return volume

    def styled_rig(self, style):
        from echo_emulator.playback import Rig
        return Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                   volume=self.styled_volume(style), module_elfs=self.module_elfs)

    def swap_round(self, style):
        from echo_emulator.modules import ModuleWatch
        rig = self.styled_rig(style)
        machine = rig.machine
        watch = ModuleWatch(rig.session)
        rig.open_songs()
        started = rig.play_song(1)
        audible = rig.first_audio_after(started)
        self.assertIsNotNone(audible)
        rig.run_for(ANIMATE_MS)
        rig.press("back")
        rig.press("back")
        rig.press_many("previous", MUSIC_ROWS_BETWEEN_SONGS_AND_COVER_FLOW)
        mark = machine.milliseconds()
        first = len(watch.loads())
        latencies = {"cover_flow": self.release_to_first_draw_ms(machine, KEY_PLAY)}
        latencies["now_playing"] = self.release_to_first_draw_ms(machine, KEY_MENU)
        rig.run_for(ANIMATE_MS)
        latencies["back_to_cover_flow"] = self.release_to_first_draw_ms(machine, KEY_BACK)
        self.release_to_first_draw_ms(machine, KEY_MENU)
        rig.run_for(ANIMATE_MS)
        rig.press("menu")
        rig.press("previous")
        latencies["up_next"] = self.release_to_first_draw_ms(machine, KEY_PLAY)
        rig.press("back")
        rig.run_for(ANIMATE_MS)
        self.assertEqual([], rig.audio.underruns(audible))
        self.assertEqual(0, rig.audio.output_underruns())
        self.assertLess(rig.longest_gap(mark), STEP_GAP_WITH_A_MODULE_MS)
        self.assertLessEqual(max(latencies.values()), MODULE_LOAD_LIMIT_MS)
        self.assert_module_code_never_written(watch)
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_healthy(rig.session)
        loads = watch.loads()[first:]
        print("\n" + json.dumps({"style": style, "release_to_first_pixels_ms": latencies,
                                 "loads": [(call["module"], round(call["us"] / 1000, 2))
                                           for call in loads],
                                 "longest_gap_ms": rig.longest_gap(mark)}))
        return rig, watch, loads

    def test_a_meter_style_swaps_to_cover_flow_and_up_next_and_back_without_stalling(self):
        rig, watch, loads = self.swap_round(VU)
        stage, _ = self.module(rig.machine, "stage")
        playing, _ = self.module(rig.machine, "playing")
        equalizer, _ = self.module(rig.machine, "equalizer")
        self.assertEqual([(call["module"], call["loaded"]) for call in loads],
                         [(playing, True), (stage, True), (playing, True), (stage, True),
                          (equalizer, True), (playing, True), (equalizer, True)])
        self.assertLess(max(call["us"] for call in loads), MODULE_LOAD_LIMIT_MS * 1000)

    def test_the_cover_style_keeps_cover_flow_and_now_playing_in_one_module(self):
        rig, watch, loads = self.swap_round(COVER)
        playing, _ = self.module(rig.machine, "playing")
        equalizer, _ = self.module(rig.machine, "equalizer")
        self.assertEqual([(call["module"], call["loaded"]) for call in loads],
                         [(equalizer, True), (playing, True), (equalizer, True)])

    def evicted_rig(self, style):
        rig = self.styled_rig(style)
        self.open_settings(rig.machine)
        rig.press("back")
        _, settings = self.module(rig.machine, "settings")
        self.assertTrue(settings.loaded(rig.machine))
        rig.press("next")
        return rig

    def test_a_damaged_stage_copy_in_slot_1_is_replaced_by_the_copy_in_slot_2(self):
        rig = self.evicted_rig(SPECTRUM)
        session, machine = rig.session, rig.machine
        number, stage = self.module(machine, "stage")
        session.change_module("slot_1", number, 600, lambda byte: byte ^ 0xFF)
        toasts = self.collect_toasts(machine)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(ANIMATE_MS)
        self.assertTrue(stage.loaded(machine))
        self.assertTrue(session.module_reads("slot_1", number))
        self.assertTrue(session.module_reads("slot_2", number))
        self.assertEqual(toasts, [])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_without_an_intact_stage_a_meter_style_plays_from_the_playing_module(self):
        from echo_emulator.modules import ModuleWatch
        rig = self.evicted_rig(VU)
        session, machine = rig.session, rig.machine
        number, _ = self.module(machine, "stage")
        playing_number, playing = self.module(machine, "playing")
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, number, 600, lambda byte: byte ^ 0xFF)
        watch = ModuleWatch(session)
        toasts = self.collect_toasts(machine)
        rig.open_songs()
        started = rig.play_song(1)
        audible = rig.first_audio_after(started)
        self.assertIsNotNone(audible)
        rig.run_for(ANIMATE_MS)
        rig.press("back")
        rig.press("next")
        rig.press("menu")
        rig.run_for(ANIMATE_MS)
        self.assertTrue(playing.loaded(machine))
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()],
                         [(number, False), (playing_number, True)])
        self.assertEqual(toasts, [])
        self.assertEqual([], rig.audio.underruns(audible))
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)


    def test_a_meter_style_swaps_only_for_the_artist_strip_and_survives_style_changes(self):
        from echo_emulator.modules import ModuleWatch
        from echo_emulator.real_volume import real_volume
        script = "\n".join(["build " + self.build_tag(), *BROWSE_SCRIPT, ""]).encode("ascii")
        volume = self.debug_volume(real_volume(playable=True, album_count=STRIP_ALBUMS),
                                   run_txt=script)
        session, machine = self.ejecting_session(volume)
        watch = ModuleWatch(session)
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        ran = report["script"]
        self.assertEqual(report["values"]["script.state"], "finished")
        self.assertEqual([line["status"] for line in ran["lines"]],
                         ["ok"] * (len(BROWSE_SCRIPT) + 1))
        screens = [event["detail"] for event in report["events"] if event["kind"] == "screen"]
        loads = [(call["module"], call["loaded"]) for call in watch.loads()]
        print("\n" + json.dumps({"screens": screens, "loads": loads}))
        stage, _ = self.module(machine, "stage")
        playing, _ = self.module(machine, "playing")
        settings, _ = self.module(machine, "settings")
        expected = [settings, stage, playing, stage, playing, stage, settings, playing, settings,
                    stage]
        self.assertEqual([(number, True) for number in expected], loads[-len(expected):])
        self.assertIn("3 " + english("STR_ALBUMS")[:7], screens)
        self.assertEqual("5 " + english("STR_NOW_PLAYING")[:7], screens[-1])
        self.assertLess(max(call["us"] for call in watch.loads()), MODULE_LOAD_LIMIT_MS * 1000)
        self.assertEqual(machine.watchdog_resets, 0)
        self.assertEqual(machine.faults, [])
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def saved_style(self, rig):
        settings = rig.session.fat().read(SETTINGS_PATH)
        return (settings[51] >> 5 & 1) | (settings[52] >> 7) << 1

    def switch_in_quick_settings(self, rig, watch, presses, first):
        rig.press("menu")
        _, equalizer = self.module(rig.machine, "equalizer")
        self.assertTrue(equalizer.loaded(rig.machine))
        if first:
            rig.press_many("previous", QUICK_SETTINGS_STYLE_ROW_FROM_END)
        opened = len(watch.loads())
        rig.press("play")
        rig.press_many("next", presses)
        rig.press("play")
        self.assertEqual(opened, len(watch.loads()))
        self.assertTrue(equalizer.loaded(rig.machine))
        windows = len(rig.machine.panel.windows)
        latency = self.release_to_first_draw_ms(rig.machine, KEY_BACK)
        rig.run_for(ANIMATE_MS)
        self.assertGreater(len(rig.machine.panel.windows), windows)
        return latency

    def playing_rig(self, style):
        from echo_emulator.modules import ModuleWatch
        rig = self.styled_rig(style)
        watch = ModuleWatch(rig.session)
        rig.open_songs()
        started = rig.play_song(1)
        audible = rig.first_audio_after(started)
        self.assertIsNotNone(audible)
        rig.run_for(ANIMATE_MS)
        return rig, watch, audible

    def test_quick_settings_switches_cover_vu_spectrum_cassette_while_playing(self):
        rig, watch, audible = self.playing_rig(COVER)
        machine = rig.machine
        stage, stage_overlay = self.module(machine, "stage")
        playing, playing_overlay = self.module(machine, "playing")
        equalizer, _ = self.module(machine, "equalizer")
        mark = machine.milliseconds()
        first = len(watch.loads())
        latencies = {}
        for style, presses, overlay in ((VU, 2, stage_overlay), (SPECTRUM, 1, stage_overlay),
                                        (CASSETTE, 2, playing_overlay)):
            latencies[style] = self.switch_in_quick_settings(rig, watch, presses, style == VU)
            self.assertEqual(style, self.saved_style(rig))
            self.assertTrue(overlay.loaded(machine))
        loads = watch.loads()[first:]
        self.assertEqual([(call["module"], call["loaded"]) for call in loads],
                         [(equalizer, True), (stage, True), (equalizer, True), (stage, True),
                          (equalizer, True), (playing, True)])
        self.assertLess(max(call["us"] for call in loads), MODULE_LOAD_LIMIT_MS * 1000)
        self.assertLessEqual(max(latencies.values()), MODULE_LOAD_LIMIT_MS)
        self.assertEqual([], rig.audio.underruns(audible))
        self.assertEqual(0, rig.audio.output_underruns())
        self.assertLess(rig.longest_gap(mark), STEP_GAP_WITH_A_MODULE_MS)
        self.assertTrue(rig.audio.running)
        self.assertEqual(1, len(rig.audio.plays))
        self.assert_module_code_never_written(watch)
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(rig.session)
        self.assert_healthy(rig.session)
        print("\n" + json.dumps({"quick_settings_close_to_first_pixels_ms": latencies,
                                 "loads": [(call["module"], round(call["us"] / 1000, 2))
                                           for call in loads],
                                 "longest_gap_ms": rig.longest_gap(mark)}))

    def test_a_meter_style_chosen_in_quick_settings_without_an_intact_stage_shows_cover(self):
        rig, watch, audible = self.playing_rig(COVER)
        session, machine = rig.session, rig.machine
        number, _ = self.module(machine, "stage")
        playing_number, playing = self.module(machine, "playing")
        equalizer, _ = self.module(machine, "equalizer")
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, number, 600, lambda byte: byte ^ 0xFF)
        toasts = self.collect_toasts(machine)
        first = len(watch.loads())
        self.switch_in_quick_settings(rig, watch, 2, True)
        self.assertEqual(VU, self.saved_style(rig))
        self.switch_in_quick_settings(rig, watch, 1, False)
        self.assertEqual(SPECTRUM, self.saved_style(rig))
        self.assertTrue(playing.loaded(machine))
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()[first:]],
                         [(equalizer, True), (number, False), (playing_number, True),
                          (equalizer, True), (playing_number, True)])
        self.assertEqual(toasts, [])
        self.assertEqual([], rig.audio.underruns(audible))
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def symbol_size(self, overlay, wanted):
        from echo_emulator.machine import make_image
        path = next(elf for elf in self.module_elfs if Path(elf).stem == overlay.name)
        elf = Path(path).read_bytes()
        header = make_image.ELF_HEADER.unpack_from(elf)
        sections = [make_image.ELF_SECTION.unpack_from(elf, header[6] + index * make_image.ELF_SECTION.size)
                    for index in range(header[12])]
        for section in sections:
            if section[1] != 2:
                continue
            names = sections[section[6]]
            for position in range(section[5] // 16):
                name, _, size, _, _, _ = struct.unpack_from("<IIIBBH", elf, section[4] + position * 16)
                start = names[4] + name
                if elf[start:elf.index(b"\x00", start)] == wanted.encode("ascii"):
                    return size
        self.fail("no symbol " + wanted)

    def reload_with_a_value_list_open(self, row, title):
        rig = self.styled_rig(COVER)
        machine, session = rig.machine, rig.session
        rig.open_songs()
        self.assertIsNotNone(rig.first_audio_after(rig.play_song(1)))
        rig.run_for(ANIMATE_MS)
        rig.press("menu")
        rig.press_many("next", row)
        rig.press("play")
        _, equalizer = self.module(machine, "equalizer")
        self.assertTrue(equalizer.loaded(machine))
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        windows = len(machine.panel.windows)
        rig.run_for(1500)
        self.assertTrue(equalizer.loaded(machine))
        self.assertGreater(len(machine.panel.windows), windows)
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.watchdog_resets, 0)
        return rig, machine, equalizer

    def test_the_equalizer_list_keeps_its_names_after_a_usb_session_reloads_the_module(self):
        rig, machine, equalizer = self.reload_with_a_value_list_open(2, "Equalizer")
        presets = equalizer.symbols["quick_presets"]
        size = self.symbol_size(equalizer, "quick_presets")
        option_count = struct.unpack("<i", machine.read_memory(presets + size - 4, 4))[0]
        self.assertGreater(option_count, 0)
        rig.press("next")
        rig.press("play")
        self.assertEqual(1, rig.session.fat().read(SETTINGS_PATH)[18])
        self.assert_healthy(rig.session)

    def test_the_headphone_list_keeps_its_names_after_a_usb_session_reloads_the_module(self):
        rig, machine, equalizer = self.reload_with_a_value_list_open(3, "Headphones")
        names = equalizer.symbols["headphone_names"]
        pointers = struct.unpack("<5I", machine.read_memory(names, 20))
        self.assertNotIn(0, pointers)
        self.assertEqual(b"Off", machine.read_cstring(pointers[0]))
        self.assertTrue(machine.read_cstring(pointers[4]))
        self.assert_healthy(rig.session)


    def wireframe_round(self, style, name):
        from echo_emulator.modules import ModuleWatch
        session = self.styled_rig(style).session.reboot()
        machine = session.machine
        watch = ModuleWatch(session)
        number, overlay = self.module(machine, name)
        frames = []
        machine.add_address_hook(overlay.symbols["wire_boot_fade"], lambda: frames.append(
            machine.milliseconds()) if overlay.loaded(machine) else None)
        session.run_to_home_screen()
        machine.run(until_ms=machine.milliseconds() + 1500)
        started = len(frames)
        self.assertGreaterEqual(started, 3)
        self.assertEqual({(call["module"], call["loaded"]) for call in watch.loads()},
                         {(number, True)})
        self.hold_power(machine)
        self.assertEqual(machine.run(until_ms=machine.milliseconds() + 8000),
                         "firmware reached hal_power_off")
        self.assertGreaterEqual(len(frames) - started, 3)
        self.assertEqual({(call["module"], call["loaded"]) for call in watch.loads()},
                         {(number, True)})
        self.assertEqual(machine.faults, [])
        return [call["module"] for call in watch.loads()]

    def test_a_meter_style_starts_and_switches_off_with_the_wireframe_from_stage(self):
        loads = self.wireframe_round(VU, "stage")
        print("\n[wireframe, VU] loads %s" % loads)

    def test_the_cover_style_starts_and_switches_off_with_the_wireframe_from_playing(self):
        loads = self.wireframe_round(COVER, "playing")
        print("\n[wireframe, Cover] loads %s" % loads)


    def test_now_playing_falls_back_safely_when_a_usb_session_evicts_stage_and_no_copy_loads(self):
        rig = self.styled_rig(VU)
        machine, session = rig.machine, rig.session
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        rig.run_for(ANIMATE_MS)
        stage, stage_overlay = self.module(machine, "stage")
        playing, _ = self.module(machine, "playing")
        self.assertTrue(stage_overlay.loaded(machine))
        for slot in ("slot_1", "slot_2"):
            for number in (stage, playing):
                session.change_module(slot, number, 600, lambda byte: byte ^ 0xFF)
        machine.note("now_playing_fallback_attach")
        machine.note("usb_session_storage_changed")
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertFalse(stage_overlay.loaded(machine))
        rig.run_for(ANIMATE_MS)
        windows = len(machine.panel.windows)
        rig.press("back")
        rig.run_for(1500)
        self.assertGreater(machine.reached.get("now_playing_fallback_attach", 0), 0)
        self.assertGreater(len(machine.panel.windows), windows)
        quiet = rig.first_audio_after(machine.milliseconds() - 300) is None
        pressed = rig.press("play")
        rig.run_for(ANIMATE_MS)
        if quiet:
            self.assertIsNotNone(rig.first_audio_after(pressed))
        else:
            self.assertGreater(rig.audio.suspend_depth, 0)
        print("\n[fallback after USB] playback %s by Play" % ("resumed" if quiet else "paused"))
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
