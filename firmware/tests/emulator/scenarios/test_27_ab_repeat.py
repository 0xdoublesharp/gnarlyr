import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    BUTTON_ACTION_AB_REPEAT,
    IDENTITY,
    PLAYBACK_MENU_HOLD,
    EchoImageHarness,
    buttons_file,
    main,
    struct,
)

MENU_HOLD_MS = 900


class EchoImageTest(EchoImageHarness):
    def test_a_mapped_menu_hold_loops_the_same_pcm_range_until_cleared(self):
        from echo_emulator.playback import Rig
        rate = 48000
        wav = bytearray(self.tagged_wav("Loop", "Loop", rate, 160, 1000))
        first = wav.index(b"data") + 8
        for frame in range(rate * 16):
            struct.pack_into("<hh", wav, first + frame * 4,
                             (frame & 32767) - 16384, (frame >> 15) - 16)
        volume = self.transition_volume([("Loop.wav", bytes(wav))], 5)
        volume.add_file("buttons.bin", buttons_file({PLAYBACK_MENU_HOLD: BUTTON_ACTION_AB_REPEAT}),
                        directory=IDENTITY.data_directory)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        samples = self.capture_output(rig, rate * 6)
        marks = []

        def marking():
            marks.append(rig.machine.milliseconds())


        actions = []

        def mark_after_mute():
            actions.append(1)
            if len(actions) == 2:
                samples.clear()

        rig.machine.hook_symbol("hal_audio_repeat_mark", marking)
        rig.machine.hook_symbol("audio_engine_repeat_mark", mark_after_mute)
        rig.open_songs()
        rig.play_song(1)
        for _ in range(2):
            rig.press("menu", hold=MENU_HOLD_MS)
        self.assertEqual(len(marks), 2)
        rig.machine.run(until=lambda: len(samples) >= rate * 5,
                        until_ms=rig.machine.milliseconds() + 9000)
        self.assertGreaterEqual(len(samples), rate * 5)
        positions = [(self.sample_value(left) // 256 + 16384) +
                     (self.sample_value(right) // 256 + 16) * 32768 for left, right in samples]
        wraps = [at for at in range(1, len(positions)) if positions[at] < positions[at - 1]]
        self.assertGreaterEqual(len(wraps), 2)
        length = wraps[1] - wraps[0]
        self.assertGreater(length, rate)
        self.assertEqual(positions[wraps[0]], positions[wraps[1]])
        for at in range(1, len(positions)):
            self.assertEqual(positions[at], positions[at - 1] + 1 if at not in wraps else positions[0])
        rig.press("menu", hold=MENU_HOLD_MS)
        self.assertEqual(len(marks), 3)
        self.assertEqual(len(rig.audio.plays), 1)
        self.assert_healthy(rig.session)

    def test_double_press_ab_repeat_while_paused_does_not_restart_dma_or_underflow(self):
        from echo_emulator.playback import Rig
        rate = 48000
        wav = self.tagged_wav("Loop", "Loop", rate, 160, 1000)
        volume = self.transition_volume([("Loop.wav", wav)], 5)
        volume.add_file("buttons.bin", buttons_file({PLAYBACK_MENU_HOLD: BUTTON_ACTION_AB_REPEAT}),
                        directory=IDENTITY.data_directory)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        starts = []
        rig.machine.hook_symbol("dmac_start_loop", lambda: starts.append(rig.machine.milliseconds()))
        rig.open_songs()
        started = rig.play_song(1)
        rig.run_for(300)
        rig.press("play")
        self.assertGreater(rig.audio.suspend_depth, 0)
        starts_before = len(starts)
        rig.press("menu", hold=MENU_HOLD_MS)
        rig.press("menu", hold=MENU_HOLD_MS)
        self.assertEqual(starts_before, len(starts))
        rig.press("play")
        self.assertEqual(0, rig.audio.suspend_depth)
        rig.run_for(400)
        self.assertEqual([], rig.audio.underruns(started))
        self.assert_healthy(rig.session)

    def test_ab_repeat_clearing_mark_a_during_fade_seeks_cleanly_without_underflow(self):
        from echo_emulator.audio import RING_BYTES
        from echo_emulator.playback import Rig
        rate = 48000
        first = self.tagged_wav("Alpha", "Album", rate, 40, 1000)
        second = self.tagged_wav("Beta", "Album", rate, 40, 2000)
        volume = self.transition_volume([("Alpha.wav", first), ("Beta.wav", second)], 0)
        volume.add_file("buttons.bin", buttons_file({PLAYBACK_MENU_HOLD: BUTTON_ACTION_AB_REPEAT}),
                        directory=IDENTITY.data_directory)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        machine = rig.machine
        mark_entries = []
        engine_entries = []
        dma_starts = []
        machine.hook_symbol("hal_audio_repeat_mark",
                            lambda: mark_entries.append((machine.milliseconds(),
                                                         rig.audio.elapsed_us(machine))))
        machine.hook_symbol("audio_engine_repeat_mark",
                            lambda: engine_entries.append(rig.audio.elapsed_us(machine)))
        machine.hook_symbol("dmac_start_loop", lambda: dma_starts.append(machine.milliseconds()))
        rig.open_songs()
        started = rig.play_song(1)
        first_press = machine.milliseconds()
        rig.press("menu", hold=MENU_HOLD_MS)
        self.assertEqual(1, len(mark_entries))
        delay_to_mark = mark_entries[0][0] - first_press
        target_ms = 4000 - delay_to_mark - 25
        self.assertGreater(target_ms, rig.audio.elapsed_us(machine) // 1000)
        self.assertEqual("condition met", machine.run(
            until=lambda: rig.audio.elapsed_us(machine) // 1000 >= target_ms,
            until_ms=machine.milliseconds() + 4000))
        starts_before = len(dma_starts)
        rig.press("menu", hold=MENU_HOLD_MS)
        self.assertEqual(2, len(mark_entries))
        self.assertEqual(2, len(engine_entries))
        self.assertLess(mark_entries[1][1], 4000000)
        self.assertGreaterEqual(engine_entries[1], 4000000)
        self.assertEqual(starts_before + 1, len(dma_starts))
        rig.run_for(400)
        self.assertEqual([], rig.audio.underruns(started))
        self.assertLessEqual(max(level for _, level in rig.audio.levels), RING_BYTES // 8)
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
