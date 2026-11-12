import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main


class EchoImageTest(EchoImageHarness):
    def test_dsf_and_dff_play_on_core1_and_queue_without_restarting_dma(self):
        from dsd_fixture import signal, dsf, dff
        from echo_emulator.core1_bridge import CORE1_DSD, Core1Bridge
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        volume = Fat32Volume()
        frames = 8820
        data, _ = signal(5644800, frames)
        volume.add_file("A.dsf", dsf(data, 5644800))
        data, _ = signal(5644800, frames)
        volume.add_file("B.dff", dff(data, 5644800))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        bridge = Core1Bridge(rig.audio, {CORE1_DSD: self.elf.with_name("core1_dsd.bin").read_bytes()})
        starts = []
        pcm = self.capture_output(rig, 2 * frames)
        rig.machine.hook_symbol("dmac_start_loop", lambda: starts.append(True))
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(700)
        self.assertEqual(2 * frames, len(pcm))
        self.assertEqual(1, len(starts))
        self.assertEqual({CORE1_DSD}, set(bridge.started))
        for frame in range(256, 2 * frames - 256, 37):
            index = frame % frames
            if index < 256 or frames - index < 256:
                continue
            for channel, frequency in enumerate((1000, 1321)):
                word = pcm[frame][channel]
                signed = word - 2 ** 32 if word & 0x80000000 else word
                actual = signed / 2 ** 31
                expected = 0.7 * math.sin(2 * math.pi * frequency * index / 88200)
                self.assertAlmostEqual(expected, actual, delta=0.005)
        self.assert_healthy(rig.session)

    def test_dsd256_mono_seeks_to_the_saved_pcm_position(self):
        from dsd_fixture import signal, dsf
        from echo_emulator.core1_bridge import CORE1_DSD, Core1Bridge
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        from unicorn.arm_const import UC_ARM_REG_R1
        frames, start = 13230, 7056
        data, _ = signal(11289600, frames, channels=1)
        volume = Fat32Volume()
        volume.add_file("Saved.dsf", dsf(data, 11289600))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        bridge = Core1Bridge(rig.audio, {CORE1_DSD: self.elf.with_name("core1_dsd.bin").read_bytes()})
        rig.machine.hook_symbol("hal_audio_play", lambda: rig.machine.uc.reg_write(UC_ARM_REG_R1, 80))
        pcm = self.capture_output(rig, frames - start)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(400)
        self.assertEqual(frames - start, len(pcm))
        self.assertEqual([CORE1_DSD], bridge.started)
        for index in range(100, len(pcm) - 100, 41):
            self.assertEqual(pcm[index][0], pcm[index][1])
            word = pcm[index][0]
            signed = word - 2 ** 32 if word & 0x80000000 else word
            expected = 0.7 * math.sin(2 * math.pi * 1000 * (start + index) / 88200)
            self.assertAlmostEqual(expected, signed / 2 ** 31, delta=0.005)
        self.assert_healthy(rig.session)

    def test_dsd256_stereo_is_refused_before_publishing_decoder_input(self):
        from dsd_fixture import signal, dff
        from echo_emulator.core1_bridge import CORE1_DSD, Core1Bridge
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        data, _ = signal(11289600, frames=3000)
        volume = Fat32Volume()
        volume.add_file("Fast.dff", dff(data, 11289600))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        bridge = Core1Bridge(rig.audio, {CORE1_DSD: self.elf.with_name("core1_dsd.bin").read_bytes()})
        pcm = self.capture_output(rig, 3000)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(300)
        self.assertEqual([], pcm)
        self.assertFalse(rig.audio.running)
        self.assertEqual(0, bridge.core.control("generation"))
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    main(EchoImageTest)
