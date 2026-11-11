import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    struct,
)


class EchoImageTest(EchoImageHarness):
    def test_shuffle_fades_out_one_album_and_fades_in_the_next_on_the_output_clock(self):
        from echo_emulator.playback import Rig
        rate, output, tenths, level = 16000, 32000, 6, 12000
        volume = self.transition_volume(
            [("A.wav", self.tagged_wav("A", "Album A", rate, 24, level)),
             ("B.wav", self.tagged_wav("B", "Album B", rate, 24, level))], tenths)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        machine = rig.machine
        boundary, length = output * 24 // 10, output * tenths // 10
        watch = self.watch_transition(rig, boundary, length)
        frames = watch["frames"]
        rig.open_songs()
        started = rig.play_song(1)
        machine.run(until=lambda: None not in frames.get(2 * boundary - 1, [None]),
                    until_ms=machine.milliseconds() + 8000)
        self.assertEqual(output, rig.audio.rate)
        self.assertEqual([], rig.audio.underruns(started))

        def gain(frame):
            return self.sample_value(frames[frame][0]) / (level * 256)

        def smooth(x):
            return x * x * (3 - 2 * x)

        for frame in range(length, boundary - length, 997):
            self.assertAlmostEqual(1.0, gain(frame), delta=0.001, msg=frame)
        for step in range(1, 8):
            position = length * step // 8
            self.assertAlmostEqual(smooth(position / length), gain(boundary - position),
                                   delta=0.002, msg=position)
            self.assertAlmostEqual(smooth(position / length), gain(boundary + position),
                                   delta=0.002, msg=position)
        self.assertAlmostEqual(1.0, gain(boundary + length + output // 10), delta=0.001)
        steps = [abs(gain(frame) - gain(frame - 1))
                 for frame in range(boundary - length, boundary + length)]
        self.assertLess(max(steps), 1.5 / length + 4 / 32768)
        for frame in range(length, 2 * boundary - length, 101):
            left, right = (self.sample_value(word) for word in frames[frame])
            self.assertLessEqual(abs(left + right), 1, frame)
        self.assertLess(self.transition_load(watch, length, "16 kHz WAV shuffle fade"), 0.2)
        self.assert_healthy(rig.session)

    def test_shuffle_fades_between_opus_albums_on_core_0(self):
        from echo_emulator.core1_bridge import CORE1_OPUS, Core1Bridge
        from echo_emulator.playback import Rig
        from unicorn.arm_const import UC_ARM_REG_SP
        tenths = 6
        volume = self.transition_volume([("A.opus", self.tagged_opus("A", "Album A")),
                                         ("B.opus", self.tagged_opus("B", "Album B"))], tenths)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        Core1Bridge(rig.audio, {CORE1_OPUS: self.elf.with_name("core1_opus.bin").read_bytes()})
        machine = rig.machine
        length, starts = 48000 * tenths // 10, []
        watch = self.watch_transition(rig, 1 << 30, length)
        frames = watch["frames"]

        def fade_in_start():
            ramp = struct.unpack("<I", machine.read_memory(machine.reg(UC_ARM_REG_SP), 4))[0]
            starts.append(struct.unpack("<I", machine.read_memory(ramp, 4))[0])

        machine.hook_symbol("audio_effects_fade_in", fade_in_start)
        rig.open_songs()
        rig.play_song(1)
        machine.run(until=lambda: starts and max(frames, default=0) >= starts[0] + 3 * length,
                    until_ms=machine.milliseconds() + 14000)
        self.assertEqual(48000, rig.audio.rate)
        self.assertTrue(starts)
        boundary = starts[0]

        def loudest(first, count):
            present = [frames[frame][0] for frame in range(first, first + count) if frame in frames]
            self.assertTrue(present, first)
            return max(abs(self.sample_value(word)) for word in present)

        def smooth(x):
            return x * x * (3 - 2 * x)

        peak = loudest(0, 48000)
        self.assertGreater(peak, 0)
        checked = 0
        for frame in range(boundary - length, boundary + length):
            if frame in frames:
                distance = boundary - frame if frame < boundary else frame - boundary
                limit = peak * smooth(distance / length) + peak / 1000
                self.assertLessEqual(abs(self.sample_value(frames[frame][0])), limit, frame)
                checked += 1
        self.assertGreater(checked, length // 2)
        self.assertGreater(loudest(boundary + 2 * length, length), peak / 2)
        self.assertGreater(watch["instructions"], 20 * length)
        print("\n[Opus shuffle fade] ramp %.1f instructions per output frame"
              % (watch["instructions"] / (2 * length)))
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
