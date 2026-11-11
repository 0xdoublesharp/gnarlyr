import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    MOTION_LOAD_LIMIT_PERCENT,
    struct,
)


class EchoImageTest(EchoImageHarness):
    def test_export_motion_leaves_core_0_room_while_44_khz_music_plays(self):
        from echo_emulator.playback import Rig
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  module_elfs=self.module_elfs)
        animated, still = self.measure_motion_load(rig, "export motion, 44.1 kHz FLAC")
        self.assertLess(animated - still, MOTION_LOAD_LIMIT_PERCENT)

    def test_export_motion_leaves_core_0_room_while_converted_music_plays(self):
        rig = self.speech_rig(22050)
        animated, still = self.measure_motion_load(rig, "export motion, 22.05 kHz WAV")
        self.assertLess(animated - still, MOTION_LOAD_LIMIT_PERCENT)

    def test_mono_speech_at_32_khz_lets_core_0_coast_without_underruns(self):
        rig = self.speech_rig()
        machine = rig.machine
        rig.open_songs()
        started = rig.play_song(1)
        self.assertEqual("condition met", machine.run(
            until=lambda: rig.audio.rate == 32000 and machine.coast_log[-1][1] == 2,
            until_ms=machine.milliseconds() + 5000))
        rig.run_for(1500)
        self.assertEqual([], rig.audio.underruns(started))
        self.assert_healthy(rig.session)

    def test_stereo_speech_conversion_keeps_core_0_at_full_speed(self):
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        rate = 16000
        pcm = b"".join(struct.pack("<hh", index % 200 - 100, 100 - index % 300)
                       for index in range(rate * 4))
        wav = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                          16, 1, 2, rate, rate * 4, 4, 16, b"data", len(pcm)) + pcm
        volume = Fat32Volume()
        volume.add_file("Stereo.wav", wav)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        machine = rig.machine
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(500)
        self.assertEqual(32000, rig.audio.rate)
        playing_since = machine.milliseconds()
        rig.run_for(2000)
        self.assertEqual([], [entry for entry in machine.coast_log
                              if entry[0] >= playing_since and entry[1] == 2])
        self.assertEqual(200_000_000, machine.core_hz)
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
