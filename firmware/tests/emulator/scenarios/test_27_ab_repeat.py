import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, struct


class EchoImageTest(EchoImageHarness):
    def test_ab_gesture_loops_the_same_pcm_range_until_cleared(self):
        from echo_emulator.playback import Rig
        rate = 48000
        wav = bytearray(self.tagged_wav("Loop", "Loop", rate, 160, 1000))
        first = wav.index(b"data") + 8
        for frame in range(rate * 16):
            struct.pack_into("<hh", wav, first + frame * 4,
                             (frame & 32767) - 16384, (frame >> 15) - 16)
        volume = self.transition_volume([("Loop.wav", bytes(wav))], 5)
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
            rig.press("play", hold=1300)
            rig.press("menu")
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
        rig.press("play", hold=1300)
        rig.press("menu")
        self.assertEqual(len(marks), 3)
        self.assertEqual(len(rig.audio.plays), 1)
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
