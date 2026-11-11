import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, struct, IDENTITY, FNV_START, FNV_PRIME
from echo_emulator.flac_image import FlacImage, BLOCK_SAMPLES


class MovingFlac(FlacImage):
    def __init__(self):
        super().__init__(700)
        original = self.body
        self.profiles = []
        for gain in (1, 2, 4, 8, 12, 8, 4, 2):
            samples = struct.pack(">%dh" % BLOCK_SAMPLES,
                                  *(int.from_bytes(original[1 + 2 * index:3 + 2 * index],
                                                   "big", signed=True) * gain // 12
                                    for index in range(BLOCK_SAMPLES)))
            self.body = (bytes([2]) + samples) * 2
            self.profiles.append((self.body, self.crc_basis()))
        self.last = (None, None)

    def frame(self, number):
        self.body, self.basis = self.profiles[number // 2 % len(self.profiles)]
        return super().frame(number)


class EchoImageTest(EchoImageHarness):
    def visual_rig(self, style, converted):
        from echo_emulator.playback import Rig
        from echo_emulator.fat_volume import Fat32Volume
        settings = bytearray(self.saved_settings(4, 15, shuffle=0))
        settings[7] = 6
        settings[51] = (style & 1) << 5
        settings[52] = (style >> 1) << 7
        checksum = FNV_START
        for value in settings[:54]:
            checksum = (checksum ^ value) * FNV_PRIME & 0xFFFFFFFF
        settings[54:58] = struct.pack("<I", checksum)
        if converted:
            rate = 16000
            pattern = struct.pack("<8h", 0, 2828, 4000, 2828, 0, -2828, -4000, -2828)
            pcm = b"".join(struct.pack("<8h", *(value * ((part // 2) % 8 + 1) // 8
                                               for value in struct.unpack("<8h", pattern))) * (rate // 10 // 8)
                           for part in range(600))
            wav = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                              16, 1, 1, rate, rate * 2, 2, 16, b"data", len(pcm)) + pcm
            volume = Fat32Volume()
            volume.add_file("Speech.wav", wav)
        else:
            volume = Fat32Volume()
            volume.add_file("Moving.flac", source=MovingFlac())
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("settings.bin", settings, directory=IDENTITY.data_directory)
        return Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                   volume=volume, module_elfs=self.module_elfs)

    def measure_style(self, style, converted):
        rig = self.visual_rig(style, converted)
        machine = rig.machine
        idle = self.idle_counter(machine)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        _, views = self.module(machine, "views")
        self.assertTrue(views.loaded(machine))
        rig.run_for(300)
        reads = machine.trace("hal_audio_visual_buffer")
        since_ms = machine.milliseconds()
        since = machine.cycles()
        animated = self.busy_percent(rig, idle, 3000)
        windows = [window for window in machine.panel.windows if window.cycles >= since and window.width < 300]
        self.assertGreater(len(windows), 0)
        frames = len([stamp for stamp in reads if stamp >= since_ms])
        self.assertGreaterEqual(frames, 29)
        self.assertLessEqual(frames, 31)
        machine.stub_symbol("meter_now_playing_animate", lambda machine: 0)
        rig.run_for(200)
        still = self.busy_percent(rig, idle, 3000)
        self.assertLessEqual(animated - still, 3)
        self.assertEqual([], rig.audio.underruns(started))
        self.assertEqual(0, rig.audio.output_underruns())
        self.assertEqual(32000 if converted else 44100, rig.audio.rate)
        self.assert_healthy(rig.session)
        print("\n[%s %s] core 0 animated %.2f %%, still %.2f %%, increase %.2f points, no underruns" %
              ("VU" if style == 2 else "Spectrum", "16 to 32 kHz" if converted else "44.1 kHz FLAC",
               animated, still, animated - still))

    def test_vu_style_leaves_core_0_room_with_44_khz_flac(self):
        self.measure_style(2, False)

    def test_vu_style_leaves_core_0_room_with_converted_16_khz(self):
        self.measure_style(2, True)

    def test_spectrum_style_leaves_core_0_room_with_44_khz_flac(self):
        self.measure_style(3, False)

    def test_spectrum_style_leaves_core_0_room_with_converted_16_khz(self):
        self.measure_style(3, True)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
