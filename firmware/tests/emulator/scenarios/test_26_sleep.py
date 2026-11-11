import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, struct


class EchoImageTest(EchoImageHarness):
    def test_sleep_fade_shapes_pcm_and_cancellation_restores_the_level(self):
        from echo_emulator.playback import Rig
        from echo_emulator.audio import PRODUCED_OFFSET
        from unicorn.arm_const import UC_ARM_REG_R0
        rate, level = 48000, 12000
        volume = self.transition_volume(
            [("Sleep.wav", self.tagged_wav("Sleep", "Sleep", rate, 80, level))], 5)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        remaining = [60000]
        deadline = [0]

        def set_remaining():
            consumed = struct.unpack("<I", rig.machine.read_memory(
                rig.audio.engine + PRODUCED_OFFSET + 4, 4))[0]
            deadline[0] = consumed + remaining[0] * rate // 1000
            rig.machine.uc.reg_write(UC_ARM_REG_R0, remaining[0])

        rig.machine.hook_symbol("hal_audio_set_sleep_remaining", set_remaining)
        samples = self.capture_output(rig, rate * 10)
        rig.open_songs()
        rig.play_song(1)
        for milliseconds, window in ((60000, 60000), (30000, 60000), (0, 60000),
                                     (0xFFFFFFFF, 0), (5000, 5000), (2500, 5000), (0, 5000)):
            remaining[0] = milliseconds
            target = len(samples) + rate
            rig.machine.run(until=lambda: len(samples) >= target,
                            until_ms=rig.machine.milliseconds() + 3000)
            self.assertGreaterEqual(len(samples), target)
            gain = self.sample_value(samples[-1][0]) / (level * 256)
            x = max(0, min(1, (deadline[0] - len(samples) + 1) * 1000 / (rate * window))) if window else 1
            expected = x * x * (3 - 2 * x)
            self.assertAlmostEqual(expected, gain, delta=0.02)
            if milliseconds == 5000:
                self.assertGreater(gain, 0.95)
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
