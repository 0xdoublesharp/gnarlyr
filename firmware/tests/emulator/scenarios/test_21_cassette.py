import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, struct, IDENTITY, fnv1a


class EchoImageTest(EchoImageHarness):
    def cassette_settings(self):
        settings = bytearray(self.saved_settings(4, 15, shuffle=0))
        settings[7] = 6
        settings[38 + 13] = 1 << 5
        checksum = fnv1a(settings[:54])
        settings[54:58] = struct.pack("<I", checksum)
        return bytes(settings)

    def cassette_rig(self, converted):
        from echo_emulator.playback import Rig
        from echo_emulator.real_volume import real_volume
        from echo_emulator.fat_volume import Fat32Volume
        if converted:
            rate = 16000
            pattern = struct.pack("<8h", 0, 2828, 4000, 2828, 0, -2828, -4000, -2828)
            pcm = pattern * (rate * 60 // 8)
            wav = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                              16, 1, 1, rate, rate * 2, 2, 16, b"data", len(pcm)) + pcm
            volume = Fat32Volume()
            volume.add_file("Speech.wav", wav)
        else:
            volume = real_volume(playable=True, album_count=2)
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("settings.bin", self.cassette_settings(), directory=IDENTITY.data_directory)
        return Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                   volume=volume, module_elfs=self.module_elfs)

    def measure_cassette(self, converted):
        rig = self.cassette_rig(converted)
        machine = rig.machine
        idle = self.idle_counter(machine)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        _, playing = self.module(machine, "playing")
        self.assertTrue(playing.loaded(machine))
        since = machine.cycles()
        animated = self.busy_percent(rig, idle, 3000)
        windows = [window for window in machine.panel.windows
                   if window.cycles >= since and window.width == 38]
        frames = len(windows) // 4
        self.assertGreaterEqual(frames, 35)
        self.assertLessEqual(frames, 39)
        machine.stub_symbol("cassette_now_playing_animate", lambda machine: 0)
        rig.run_for(200)
        since = machine.cycles()
        still = self.busy_percent(rig, idle, 3000)
        self.assertEqual([], [window for window in machine.panel.windows
                              if window.cycles >= since and window.width == 38])
        self.assertLess(animated - still, 5)
        self.assertEqual([], rig.audio.underruns(started))
        self.assert_healthy(rig.session)
        print("\n[cassette %s] core 0 animated %.1f %%, still %.1f %%, increase %.1f points, "
              "%d frames in 3000 ms, output %d Hz, no underruns" %
              ("16 kHz converted" if converted else "44.1 kHz FLAC", animated, still,
               animated - still, frames, rig.audio.rate))

    def test_cassette_motion_leaves_core_0_room_with_44_khz_flac(self):
        self.measure_cassette(False)

    def test_cassette_motion_leaves_core_0_room_with_converted_16_khz(self):
        self.measure_cassette(True)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
