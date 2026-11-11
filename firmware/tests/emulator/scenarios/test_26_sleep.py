import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main


class EchoImageTest(EchoImageHarness):
    def test_sleep_fade_shapes_pcm_and_cancellation_restores_the_level(self):
        from echo_emulator.playback import Rig
        from unicorn.arm_const import UC_ARM_REG_R0
        rate, level = 48000, 12000
        volume = self.transition_volume(
            [("Sleep.wav", self.tagged_wav("Sleep", "Sleep", rate, 80, level))], 5)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        remaining = [60000]
        rig.machine.hook_symbol("hal_audio_set_sleep_remaining", lambda:
                                rig.machine.uc.reg_write(UC_ARM_REG_R0, remaining[0]))
        samples = self.capture_output(rig, rate * 8)
        rig.open_songs()
        rig.play_song(1)
        for milliseconds, expected in ((60000, 1), (30000, 0.5), (0, 0), (0xFFFFFFFF, 1)):
            remaining[0] = milliseconds
            target = len(samples) + rate
            rig.machine.run(until=lambda: len(samples) >= target,
                            until_ms=rig.machine.milliseconds() + 3000)
            self.assertGreaterEqual(len(samples), target)
            gain = self.sample_value(samples[-1][0]) / (level * 256)
            self.assertAlmostEqual(expected, gain, delta=0.02)
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
