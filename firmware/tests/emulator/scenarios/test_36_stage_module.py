import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from echo_image_harness import (
    PLAYBACK_SAFE_START_ROW,
    SETTINGS_PLAYBACK_GROUP,
    SOUND_EQUALIZER_ROW,
    DAC_PROMPT_KEYS,
    EchoImageHarness,
    IDENTITY,
    KEY_BACK,
    KEY_MENU,
    KEY_NEXT,
    KEY_PLAY,
    MODULE_LOAD_LIMIT_MS,
    MUSIC_ROWS_BETWEEN_SONGS_AND_COVER_FLOW,
    SETTINGS_PATH,
    STEP_GAP_WITH_A_MODULE_MS,
    english,
    fnv1a,
    main,
    seal_settings,
    struct,
)
from test_22_meter_styles import MovingFlac

COVER, CASSETTE, VU, SPECTRUM = 0, 1, 2, 3
QUICK_SETTINGS_STYLE_ROW_FROM_END = 3
QUICK_SETTINGS_EQUALIZER_ROW = 2
QUICK_SETTINGS_HEADPHONES_ROW = 3
SETTINGS_EQUALIZER_FIELD = 18
SETTINGS_LONG_PRESS_FIELD = 59
LONG_PRESS_SHUFFLE = 4
MENU_HOLD_MS = 900
EQUALIZER_PRESETS = 9
EQUALIZER_SLOTS_FILE = "eq_slots.bin"
EQUALIZER_GAIN_MAX = 20
HAL_USB_ANSWER_DAC = 3
SETTLE_AFTER_STORAGE_CHANGE_MS = 4000
SETTINGS_EXTENSION = 38
PARAMETRIC_FILE = "parametric.bin"
PARAMETRIC_RECORD = 84
PARAMETRIC_BANK_BYTES = 1536
STORE_BLOCK = 512
SCREENSHOT_SCRIPT = ["open coverflow", "wait 600", "rescan check", "screenshot after_check",
                     "reconnect"]
DAC_MENU_HEADPHONES_ROW = 3

ANIMATE_MS = 600
STRIP_ALBUMS = 3
BROWSE_SCRIPT = ["set Now Playing style=VU meters", "play", "down,down,play", "wait 600", "play",
                 "wait 900", "play", "wait 700", "back", "wait 700", "back", "wait 700", "back",
                 "wait 400", "down,down,play", "wait 600", "play", "wait 700",
                 "set Now Playing style=Cassette", "wait 300", "play", "wait 900", "menu",
                 "wait 900", "screenshot cassette", "set Now Playing style=VU meters", "wait 900",
                 "screenshot vu", "reconnect"]


class EchoImageTest(EchoImageHarness):
    def styled_volume(self, style, fields=None, files=None):
        from echo_emulator.fat_volume import Fat32Volume
        settings = bytearray(self.saved_settings(4, 15, shuffle=0))
        settings[7] = 6
        settings[51] = (style & 1) << 5
        settings[52] = (style >> 1) << 7
        for offset, value in (fields or {}).items():
            settings[offset] = value
        volume = Fat32Volume()
        volume.add_file("Moving.flac", source=MovingFlac())
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("settings.bin", seal_settings(settings), directory=IDENTITY.data_directory)
        for name, data in (files or {}).items():
            volume.add_file(name, data, directory=IDENTITY.data_directory)
        return volume

    def styled_rig(self, style, fields=None, files=None, prepare=None):
        from echo_emulator.playback import Rig
        return Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                   volume=self.styled_volume(style, fields, files), module_elfs=self.module_elfs,
                   prepare=prepare)

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
        machine.note("usb_session_storage_changed")
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertFalse(equalizer.loaded(machine))
        machine.vbus = False
        windows = len(machine.panel.windows)
        rig.run_for(1500)
        self.assertTrue(equalizer.loaded(machine))
        self.assertGreater(len(machine.panel.windows), windows)
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.watchdog_resets, 0)
        return rig, machine, equalizer

    def test_the_equalizer_list_keeps_its_names_after_a_usb_session_reloads_the_module(self):
        rig, machine, equalizer = self.reload_with_a_value_list_open(2, "Equalizer")
        self.assertEqual(EQUALIZER_PRESETS + 1, self.option_count(machine, equalizer))
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


    def start_playing(self, rig):
        rig.open_songs()
        self.assertIsNotNone(rig.first_audio_after(rig.play_song(1)))

    def equalizer_slots(self, used):
        data = bytearray(b"EQSL")
        for slot in range(8):
            data += bytes((EQUALIZER_GAIN_MAX + slot,) * 10) if slot in used else b"\xff" * 10
        return bytes(data + struct.pack("<I", fnv1a(data)))

    def option_count(self, machine, equalizer):
        presets = equalizer.symbols["presets"]
        size = self.symbol_size(equalizer, "presets")
        return struct.unpack("<i", machine.read_memory(presets + size - 4, 4))[0]

    def headphone_names(self, machine, equalizer):
        pointers = struct.unpack("<5I", machine.read_memory(equalizer.symbols["headphone_names"], 20))
        self.assertNotIn(0, pointers)
        return [machine.read_cstring(pointer) for pointer in pointers]

    def open_list(self, rig, row):
        rig.press("menu")
        rig.press_many("next", row)
        rig.press("play")
        _, equalizer = self.module(rig.machine, "equalizer")
        self.assertTrue(equalizer.loaded(rig.machine))
        return equalizer

    def end_usb_session(self, rig, equalizer):
        machine, session = rig.machine, rig.session
        machine.note("usb_session_storage_changed")
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertFalse(equalizer.loaded(machine))
        return session

    def unplug_and_settle(self, rig, equalizer):
        machine = rig.machine
        machine.vbus = False
        windows = len(machine.panel.windows)
        rig.run_for(1500)
        self.assertTrue(equalizer.loaded(machine))
        self.assertGreater(len(machine.panel.windows), windows)

    def assert_clean(self, rig, watch):
        machine = rig.machine
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_module_code_never_written(watch)
        self.assert_only_the_user_volume_changed(rig.session)
        self.assert_healthy(rig.session)

    def test_a_menu_hold_shortcut_over_the_equalizer_list_reloads_it_before_it_runs(self):
        from echo_emulator.modules import ModuleWatch
        rig = self.styled_rig(COVER, {SETTINGS_LONG_PRESS_FIELD: LONG_PRESS_SHUFFLE})
        machine = rig.machine
        self.start_playing(rig)
        rig.run_for(ANIMATE_MS)
        equalizer = self.open_list(rig, QUICK_SETTINGS_EQUALIZER_ROW)
        settings_number, _ = self.module(machine, "settings")
        equalizer_number, _ = self.module(machine, "equalizer")
        watch = ModuleWatch(rig.session)
        shuffle = rig.session.fat().read(SETTINGS_PATH)[11]
        rig.press("menu", hold=MENU_HOLD_MS)
        rig.run_for(ANIMATE_MS)
        self.assertNotEqual(shuffle, rig.session.fat().read(SETTINGS_PATH)[11])
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()],
                         [(settings_number, True), (equalizer_number, True)])
        self.assertTrue(equalizer.loaded(machine))
        self.assertEqual(EQUALIZER_PRESETS + 1, self.option_count(machine, equalizer))
        rig.press("next")
        rig.press("play")
        self.assertEqual(1, rig.session.fat().read(SETTINGS_PATH)[SETTINGS_EQUALIZER_FIELD])
        self.assert_clean(rig, watch)

    def test_a_dac_headphone_change_over_the_headphone_list_reloads_it_before_drawing(self):
        from echo_emulator.modules import ModuleWatch
        rig = self.styled_rig(COVER, prepare=lambda machine: setattr(machine, "usb_answer",
                                                                     HAL_USB_ANSWER_DAC))
        machine = rig.machine
        self.start_playing(rig)
        rig.run_for(ANIMATE_MS)
        equalizer = self.open_list(rig, QUICK_SETTINGS_HEADPHONES_ROW)
        rig.press("next")
        rig.press("play")
        rig.press("play")
        self.assertEqual(b"Off", self.headphone_names(machine, equalizer)[0])
        watch = ModuleWatch(rig.session)
        machine.host_stays = True
        machine.note("usb_dac_session")
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: "usb_dac_session" in machine.reached,
                                     until_ms=machine.milliseconds() + 6000), "condition met")
        machine.run(until_ms=machine.milliseconds() + 500)
        for key in (KEY_MENU,) + (KEY_NEXT,) * DAC_MENU_HEADPHONES_ROW + (KEY_PLAY, KEY_MENU):
            self.press(machine, key)
        machine.host_stays = False
        self.unplug_and_settle(rig, equalizer)
        settings_number, _ = self.module(machine, "settings")
        equalizer_number, _ = self.module(machine, "equalizer")
        loads = [(call["module"], call["loaded"]) for call in watch.loads()]
        self.assertIn((settings_number, True), loads)
        self.assertEqual((equalizer_number, True), loads[-1])
        names = self.headphone_names(machine, equalizer)
        self.assertEqual(english("STR_OFF").encode("utf-8"), names[0])
        self.assertTrue(all(names[1:]))
        rig.press("play")
        rig.run_for(ANIMATE_MS)
        self.assertTrue(equalizer.loaded(machine))
        self.assert_clean(rig, watch)

    def test_the_settings_equalizer_browser_keeps_its_names_after_a_usb_session(self):
        from echo_emulator.modules import ModuleWatch
        rig = self.styled_rig(COVER)
        machine = rig.machine
        rig.session.run_to_home_screen()
        self.open_settings(machine)
        rig.press("play")
        rig.press_many("next", SOUND_EQUALIZER_ROW)
        rig.press("play")
        _, equalizer = self.module(machine, "equalizer")
        self.assertTrue(equalizer.loaded(machine))
        watch = ModuleWatch(rig.session)
        self.end_usb_session(rig, equalizer)
        self.unplug_and_settle(rig, equalizer)
        self.assertEqual(EQUALIZER_PRESETS + 1, self.option_count(machine, equalizer))
        rig.press("next")
        rig.press("play")
        self.assertEqual(1, rig.session.fat().read(SETTINGS_PATH)[SETTINGS_EQUALIZER_FIELD])
        self.assert_clean(rig, watch)

    def test_a_slot_file_failure_after_a_reload_clamps_the_equalizer_list(self):
        from echo_emulator.modules import ModuleWatch
        rig = self.styled_rig(COVER, files={EQUALIZER_SLOTS_FILE: self.equalizer_slots({2, 5})})
        machine = rig.machine
        self.start_playing(rig)
        rig.run_for(ANIMATE_MS)
        equalizer = self.open_list(rig, QUICK_SETTINGS_EQUALIZER_ROW)
        self.assertEqual(EQUALIZER_PRESETS + 3, self.option_count(machine, equalizer))
        rig.press_many("next", EQUALIZER_PRESETS + 1)
        watch = ModuleWatch(rig.session)
        session = self.end_usb_session(rig, equalizer)
        slots = "/" + IDENTITY.data_directory + "/" + EQUALIZER_SLOTS_FILE
        session.disk.write(session.file_sectors(slots).start, b"\xa5" * 512)
        self.unplug_and_settle(rig, equalizer)
        rig.run_for(SETTLE_AFTER_STORAGE_CHANGE_MS)
        self.assertTrue(equalizer.loaded(machine))
        self.assertEqual(EQUALIZER_PRESETS + 1, self.option_count(machine, equalizer))
        rig.press_many("previous", 3)
        rig.press("play")
        rig.run_for(ANIMATE_MS)
        self.assertEqual(-3 % EQUALIZER_PRESETS,
                         rig.session.fat().read(SETTINGS_PATH)[SETTINGS_EQUALIZER_FIELD])
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_healthy(rig.session)

    def parametric_bank(self, used):
        record = bytearray(PARAMETRIC_RECORD)
        struct.pack_into("<BBh", record, 0, 1, 0, -20)
        struct.pack_into("<HHhBB", record, 4, 1000, 71, 15, 0, 1)
        slots = b"".join(bytes(record) if slot in used else bytes(PARAMETRIC_RECORD)
                         for slot in range(8))
        mask = sum(1 << slot for slot in used)
        payload = bytes((mask, 0, 0, 0)) + bytes(record) + slots + bytes(9 * 9)
        header = struct.pack("<4sHHIIII", b"PEQF", 1, 0, 1, len(payload), fnv1a(payload), 0)
        header += struct.pack("<I", fnv1a(header))
        bank = header + bytes(STORE_BLOCK - len(header)) + payload
        return bank + bytes(2 * PARAMETRIC_BANK_BYTES - len(bank))

    def parametric_slots(self, session):
        data = session.fat().read("/" + IDENTITY.data_directory + "/" + PARAMETRIC_FILE)
        newest = None
        for offset in (0, PARAMETRIC_BANK_BYTES):
            magic, _, _, generation, length = struct.unpack_from("<4sHHII", data, offset)
            if magic == b"PEQF" and (newest is None or generation > newest[0]):
                newest = (generation, data[offset + STORE_BLOCK:offset + STORE_BLOCK + length])
        payload = newest[1]
        return {slot for slot in range(8) if payload[0] >> slot & 1}

    def graphic_slots(self, session):
        data = session.fat().read("/" + IDENTITY.data_directory + "/" + EQUALIZER_SLOTS_FILE)
        return {slot for slot in range(8) if data[4 + 10 * slot] != 0xFF}

    def test_a_failed_menu_hold_shortcut_over_a_list_never_runs_the_cleared_slot(self):
        from echo_emulator.modules import ModuleWatch
        rig = self.styled_rig(COVER, {SETTINGS_LONG_PRESS_FIELD: LONG_PRESS_SHUFFLE})
        machine, session = rig.machine, rig.session
        self.start_playing(rig)
        rig.run_for(ANIMATE_MS)
        equalizer = self.open_list(rig, QUICK_SETTINGS_EQUALIZER_ROW)
        settings_number, _ = self.module(machine, "settings")
        equalizer_number, _ = self.module(machine, "equalizer")
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, settings_number, 600, lambda byte: byte ^ 0xFF)
        watch = ModuleWatch(session)
        shuffle = session.fat().read(SETTINGS_PATH)[11]
        rig.press("menu", hold=MENU_HOLD_MS)
        rig.run_for(ANIMATE_MS)
        self.assertEqual(machine.faults, [])
        self.assertEqual(shuffle, session.fat().read(SETTINGS_PATH)[11])
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()],
                         [(settings_number, False), (equalizer_number, True)])
        self.assertEqual(EQUALIZER_PRESETS + 1, self.option_count(machine, equalizer))
        rig.press("next")
        rig.press("play")
        self.assertEqual(1, session.fat().read(SETTINGS_PATH)[SETTINGS_EQUALIZER_FIELD])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_healthy(session)

    def test_a_screenshot_right_after_a_music_check_draws_from_the_screen_owner(self):
        from echo_emulator.real_volume import real_volume
        script = "\n".join(["build " + self.build_tag(), *SCREENSHOT_SCRIPT, ""]).encode("ascii")
        volume = self.debug_volume(real_volume(playable=True, album_count=STRIP_ALBUMS),
                                   run_txt=script)
        session, machine = self.ejecting_session(volume)
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        ran = report["script"]
        self.assertEqual([line["status"] for line in ran["lines"]],
                         ["ok"] * (len(SCREENSHOT_SCRIPT) + 1))
        self.assertEqual(ran["screenshots"], [{"slot": 0, "name": "after_check"}])
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_healthy(session)

    def test_the_safe_start_volume_list_keeps_its_labels_across_a_dac_session(self):
        rig = self.styled_rig(COVER, prepare=lambda machine: setattr(machine, "usb_answer",
                                                                     HAL_USB_ANSWER_DAC))
        machine, session = rig.machine, rig.session
        self.open_settings(machine)
        rig.press_many("next", SETTINGS_PLAYBACK_GROUP)
        rig.press("play")
        rig.press_many("next", PLAYBACK_SAFE_START_ROW)
        rig.press("play")
        _, settings = self.module(machine, "settings")
        self.assertTrue(settings.loaded(machine))
        before = session.fat().read(SETTINGS_PATH)[SETTINGS_EXTENSION:SETTINGS_EXTENSION + 2]
        machine.host_stays = True
        machine.note("usb_dac_session")
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: "usb_dac_session" in machine.reached,
                                     until_ms=machine.milliseconds() + 6000), "condition met")
        machine.run(until_ms=machine.milliseconds() + 500)
        machine.host_stays = False
        self.unplug_and_settle(rig, settings)
        self.assertEqual(machine.faults, [])
        rig.press("next")
        rig.press("play")
        after = session.fat().read(SETTINGS_PATH)[SETTINGS_EXTENSION:SETTINGS_EXTENSION + 2]
        self.assertNotEqual(before, after)
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_healthy(session)

    def test_a_wireframe_goodbye_with_damaged_playing_copies_tries_the_module_once(self):
        from echo_emulator.modules import ModuleWatch
        rig = self.styled_rig(COVER)
        machine, session = rig.machine, rig.session
        playing_number, _ = self.module(machine, "playing")
        self.open_settings(machine)
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, playing_number, 600, lambda byte: byte ^ 0xFF)
        rig.press("back")
        rig.run_for(ANIMATE_MS)
        watch = ModuleWatch(session)
        self.hold_power(machine)
        self.assertEqual(machine.run(until_ms=machine.milliseconds() + 8000),
                         "firmware reached hal_power_off")
        attempts = [call for call in watch.loads() if call["module"] == playing_number]
        self.assertLessEqual(len(attempts), 1)
        self.assertEqual(machine.faults, [])

    def test_an_eq_edit_after_an_eviction_keeps_every_other_saved_slot(self):
        from echo_emulator.modules import ModuleWatch
        files = {EQUALIZER_SLOTS_FILE: self.equalizer_slots({0, 2}),
                 PARAMETRIC_FILE: self.parametric_bank({1, 3})}
        rig = self.styled_rig(COVER, {SETTINGS_LONG_PRESS_FIELD: LONG_PRESS_SHUFFLE}, files)
        machine, session = rig.machine, rig.session
        self.assertEqual({1, 3}, self.parametric_slots(session))
        self.open_settings(machine)
        rig.press("play")
        rig.press_many("next", SOUND_EQUALIZER_ROW)
        rig.press("play")
        rig.press("previous")
        rig.press("play")
        _, equalizer = self.module(machine, "equalizer")
        self.assertTrue(equalizer.loaded(machine))
        settings_number, _ = self.module(machine, "settings")
        equalizer_number, _ = self.module(machine, "equalizer")
        watch = ModuleWatch(session)
        rig.press("menu", hold=MENU_HOLD_MS)
        rig.run_for(ANIMATE_MS)
        loads = [(call["module"], call["loaded"]) for call in watch.loads()]
        self.assertEqual((settings_number, True), loads[0])
        self.assertEqual((equalizer_number, True), loads[-1])
        rig.press("play")
        rig.press("next")
        rig.press("next")
        rig.press("play")
        rig.run_for(1000)
        self.assertTrue({1, 3} <= self.parametric_slots(session))
        self.assertEqual({0, 2}, self.graphic_slots(session))
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_healthy(session)

    def wireframe_round(self, style, name):
        from echo_emulator.modules import ModuleWatch
        session = self.styled_rig(style).session.reboot()
        machine = session.machine
        watch = ModuleWatch(session)
        number, overlay = self.module(machine, name)
        start, _ = self.module(machine, "trial")
        frames = []
        machine.add_address_hook(overlay.symbols["wire_boot_fade"], lambda: frames.append(
            machine.milliseconds()) if overlay.loaded(machine) else None)
        session.run_to_home_screen()
        machine.run(until_ms=machine.milliseconds() + 1500)
        started = len(frames)
        self.assertGreaterEqual(started, 3)
        self.assertEqual({(call["module"], call["loaded"]) for call in watch.loads()
                          if call["module"] != start}, {(number, True)})
        self.hold_power(machine)
        self.assertEqual(machine.run(until_ms=machine.milliseconds() + 8000),
                         "firmware reached hal_power_off")
        self.assertGreaterEqual(len(frames) - started, 3)
        self.assertEqual({(call["module"], call["loaded"]) for call in watch.loads()
                          if call["module"] != start}, {(number, True)})
        self.assertEqual(machine.faults, [])
        return [call["module"] for call in watch.loads() if call["module"] != start]

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


    def test_now_playing_falls_back_and_stops_retrying_after_a_usb_dac_session_with_no_copy(self):
        from echo_emulator.modules import ModuleWatch
        rig = self.styled_rig(VU, prepare=lambda machine: setattr(machine, "usb_answer", None))
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
        machine.note("usb_dac_session")
        machine.host_stays = True
        machine.answer_usb_prompt(DAC_PROMPT_KEYS)
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: "usb_dac_session" in machine.reached,
                                     until_ms=machine.milliseconds() + 6000), "condition met")
        rig.run_for(ANIMATE_MS)
        self.assertFalse(stage_overlay.loaded(machine))
        windows = len(machine.panel.windows)
        machine.vbus = False
        rig.run_for(1500)
        watch = ModuleWatch(session)
        rig.run_for(4000)
        failed = [call for call in watch.loads() if not call["loaded"]]
        print("\n[after the DAC session] %d failed loads in 4 s, fallback attached %d times"
              % (len(failed), machine.reached.get("now_playing_fallback_attach", 0)))
        self.assertGreater(machine.reached.get("now_playing_fallback_attach", 0), 0)
        self.assertLessEqual(len(failed), 3)
        self.assertGreater(len(machine.panel.windows), windows)
        quiet = rig.first_audio_after(machine.milliseconds() - 300) is None
        pressed = rig.press("play")
        rig.run_for(ANIMATE_MS)
        if quiet:
            self.assertIsNotNone(rig.first_audio_after(pressed))
        else:
            self.assertGreater(rig.audio.suspend_depth, 0)
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
