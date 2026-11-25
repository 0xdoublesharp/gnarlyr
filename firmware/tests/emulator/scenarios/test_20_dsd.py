import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main


class EchoImageTest(EchoImageHarness):
    def assert_dsd_waveform(self, tracks, data, rate):
        from dsd_filter import cic_coefficients, compensation

        ratio = rate // 2822400
        step = 16 * ratio
        cic = cic_coefficients(ratio)
        fir = compensation(ratio)
        weights = [0.0] * ((len(fir) - 1) * step + len(cic))
        for stage, coefficient in enumerate(fir):
            for tap, value in enumerate(cic):
                weights[stage * step + tap] += coefficient * value
        for channel, encoded in enumerate(data):
            bits = [2 * ((byte >> bit) & 1) - 1 for byte in encoded for bit in range(8)]
            for frame in range(256, len(tracks[0]) - 256, 29):
                end = (34 + 2 * frame) * step - 1
                expected = 2 * sum(weight * bits[end - tap] for tap, weight in enumerate(weights))
                for pcm in tracks:
                    word = pcm[frame][channel]
                    signed = word - 2 ** 32 if word & 0x80000000 else word
                    self.assertAlmostEqual(expected, signed / 2 ** 31, delta=32 / 2 ** 23,
                                           msg=f"frame {frame}, channel {channel}")

    def test_dsf_and_dff_play_on_core1_and_queue_without_restarting_dma(self):
        from dsd_fixture import signal, dsf, dff
        from echo_emulator.core1_bridge import CORE1_DSD, Core1Bridge
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        volume = Fat32Volume()
        frames = 8820
        data, _ = signal(5644800, frames)
        volume.add_file("A.dsf", dsf(data, 5644800))
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
        self.assert_dsd_waveform((pcm[:frames], pcm[frames:]), data, 5644800)
        self.assert_healthy(rig.session)

    def test_dsd64_stereo_dsf_plays_the_reference_sine_across_blocks(self):
        from dsd_fixture import signal, dsf
        from echo_emulator.core1_bridge import CORE1_DSD, Core1Bridge
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        frames = 13230
        data, samples = signal(2822400, frames)
        volume = Fat32Volume()
        volume.add_file("Sine.dsf", dsf(data, 2822400, samples))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        bridge = Core1Bridge(rig.audio, {CORE1_DSD: self.elf.with_name("core1_dsd.bin").read_bytes()})
        rig.machine.stub_symbol("listening_effects_choose", lambda machine: 1 << 8)
        pcm = self.capture_output(rig, frames)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(400)
        self.assertEqual(frames, len(pcm))
        self.assertEqual([CORE1_DSD], bridge.started)
        self.assert_dsd_waveform((pcm,), data, 2822400)
        for channel, frequency in enumerate((1000, 1321)):
            words = [frame[channel] for frame in pcm[256:-256]]
            self.assertAlmostEqual(0.7, 2 * self.tone_level(words, 88200, frequency), delta=0.01)
        self.assert_healthy(rig.session)

    def test_dsd64_stereo_keeps_up_with_the_players_audio_service_cadence(self):
        from dsd_fixture import signal, dsf
        from echo_emulator.core1_bridge import CORE1_DSD, Core1Bridge
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        frames, window = 44100, 4410
        data, samples = signal(2822400, frames + 2 * window)
        volume = Fat32Volume()
        volume.add_file("Sine.dsf", dsf(data, 2822400, samples))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        program = self.elf.with_name("core1_dsd.bin").read_bytes()
        bridge = Core1Bridge(rig.audio, {CORE1_DSD: program}, instructions_per_us=150)
        rig.machine.stub_symbol("listening_effects_choose", lambda machine: 1 << 8)
        self.limit_audio_service(rig, 10000)
        played = self.capture_played(rig, frames)
        rig.open_songs()
        rig.play_song(1)
        for _ in range(40):
            if len(played) == frames:
                break
            rig.run_for(50)
        self.assertEqual(frames, len(played))
        self.assertEqual([CORE1_DSD], bridge.started)
        for start in range(window, frames - window + 1, window):
            for channel, frequency in enumerate((1000, 1321)):
                words = [frame[channel] for frame in played[start:start + window]]
                self.assertGreater(self.tone_level(words, 88200, frequency), 0.34, (start, channel))
        self.assert_healthy(rig.session)

    def test_dop64_dma_preserves_markers_and_switches_to_pcm_when_effects_turn_on(self):
        from dsd_fixture import signal, dff
        from echo_emulator.core1_bridge import CORE1_DSD, Core1Bridge
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        data, _ = signal(2822400, frames=88200)
        volume = Fat32Volume()
        volume.add_file("Native.dff", dff(data, 2822400))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        program = self.elf.with_name("core1_dsd.bin").read_bytes()
        bridge = Core1Bridge(rig.audio, {CORE1_DSD: program}, instructions_per_us=100)
        effects = [0]
        rig.machine.stub_symbol("listening_effects_choose", lambda machine: effects[0])
        frames = self.capture_output(rig, 9000)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        rig.run_for(120)
        self.assertEqual(176400, rig.audio.rate)
        self.assertEqual(9000, len(frames))
        order = bytes(int(f"{value:08b}"[::-1], 2) for value in range(256))
        for channel in (0, 1):
            payload = data[channel][:2 * len(frames)].translate(order)
            for at, frame in enumerate(frames):
                marker = 0x05 if at % 2 == 0 else 0xFA
                word = marker << 24 | payload[2 * at] << 16 | payload[2 * at + 1] << 8
                self.assertEqual(word, frame[channel], (channel, at))
        effects[0] = 1
        rig.run_for(600)
        self.assertEqual(88200, rig.audio.rate)
        self.assertEqual([CORE1_DSD], bridge.started)
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
    sys.exit(main(EchoImageTest))
