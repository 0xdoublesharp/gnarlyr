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
    STEP_GAP_WITH_A_MODULE_MS,
    fnv1a,
    main,
    struct,
)
from test_22_meter_styles import MovingFlac

COVER, VU, SPECTRUM = 0, 2, 3
ANIMATE_MS = 600


class EchoImageTest(EchoImageHarness):
    def styled_rig(self, style):
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        settings = bytearray(self.saved_settings(4, 15, shuffle=0))
        settings[7] = 6
        settings[51] = (style & 1) << 5
        settings[52] = (style >> 1) << 7
        settings[54:58] = struct.pack("<I", fnv1a(settings[:54]))
        volume = Fat32Volume()
        volume.add_file("Moving.flac", source=MovingFlac())
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("settings.bin", settings, directory=IDENTITY.data_directory)
        return Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                   volume=volume, module_elfs=self.module_elfs)

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


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
