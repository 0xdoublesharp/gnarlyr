import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    IDENTITY,
    PROJECT,
    main,
    MOTION_LOAD_LIMIT_PERCENT,
    struct,
    IDENTITY,
    SETTINGS_PATH,
)

SPEED_STEPS = ((100, 0), (150, 2), (200, 4), (300, 6))
FULL_RING_WORDS = 7 * 4000
STRETCH_RING_WORDS = (FULL_RING_WORDS - (6002 * 6 + 3) // 4) // 10 * 10
SPEED_SETTLE_MS = 1000
SPEED_MEASURE_MS = 2000
CORE1_KEEPUP_SLICES = 128


class EchoImageTest(EchoImageHarness):
    def parametric_file(self):
        def checksum(data):
            value = 2166136261
            for byte in data:
                value = (value ^ byte) * 16777619 & 0xFFFFFFFF
            return value
        record = bytearray(84)
        struct.pack_into("<BBh", record, 0, 10, 0, -20)
        for index, hz in enumerate((31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000)):
            struct.pack_into("<HHhBB", record, 4 + 8 * index, hz, 71, 15,
                             index % 3, 1)
        payload = bytes((1, 0, 0, 0)) + record + record + bytes(7 * 84)
        header = struct.pack("<4sHHIIII", b"PEQF", 1, 0, 1, len(payload), checksum(payload), 0)
        header += struct.pack("<I", checksum(header))
        return header + bytes(512 - len(header)) + payload + bytes(3072 - 512 - len(payload))

    def test_parametric_slot_matches_graphic_core_load_and_switches_without_underruns(self):
        measurements = []
        for parametric in (False, True):
            files = (("parametric.bin", self.parametric_file()),) if parametric else ()
            rig, _ = self.replay_gain_rig(0, equalizer=8 if parametric else 7,
                                         gains=(3,) * 10, extra_files=files)
            machine = rig.machine
            idle = self.idle_counter(machine)
            starts = machine.trace("dmac_start_loop")
            runs = []
            machine.hook_symbol("audio_process_equalizer_run", lambda: runs.append(machine.milliseconds()))
            rig.open_songs()
            mark = rig.play_song(1)
            self.assertIsNotNone(rig.first_audio_after(mark))
            rig.run_for(500)
            busy = self.busy_percent(rig, idle, 1000)
            measurements.append(busy)
            print("\n[EQ] %s core 0 busy %.2f %%" % ("parametric" if parametric else "graphic", busy), flush=True)
            self.assertTrue(runs)
            self.assertEqual([], rig.audio.underruns(mark))
            if parametric:
                rig.press("back", hold=1300)
                rig.press("previous")
                rig.press("play")
                rig.press_many("next", 17)
                rig.press("play")
                rig.press("play")
                self.assertEqual(8, rig.session.fat().read(SETTINGS_PATH)[18])
                rig.press("back")
                rig.press("play")
                rig.press_many("previous", 9)
                rig.press("play")
                self.assertEqual(0, rig.session.fat().read(SETTINGS_PATH)[18])
                rig.run_for(500)
                self.assertEqual([], rig.audio.underruns(mark))
                self.assertEqual(1, len(starts))
            self.assert_healthy(rig.session)
        print("\n[EQ core 0 at 44.1 kHz, graphic nine / parametric ten sections] graphic %.2f %%, parametric %.2f %%, delta %.2f pp" %
              (measurements[0], measurements[1], measurements[1] - measurements[0]))
        self.assertLess(measurements[1] - measurements[0], 3.0)

    def speed_volume(self, files, step):
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.real_volume import MEGABYTES
        volume = Fat32Volume(MEGABYTES)
        for file in files:
            volume.add_file(*file)
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("settings.bin", self.saved_settings(0x07, 15, 0, step),
                        directory=IDENTITY.data_directory)
        return volume

    def measure_speed_load(self, files, label, rate):
        from echo_emulator.playback import Rig
        loads = []
        for percent, step in SPEED_STEPS:
            rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                      volume=self.speed_volume(files, step), module_elfs=self.module_elfs)
            rig.audio.core1.slices = CORE1_KEEPUP_SLICES
            idle = self.idle_counter(rig.machine)
            rig.open_songs()
            started = rig.play_song(1)
            self.assertIsNotNone(rig.first_audio_after(started))
            rig.run_for(SPEED_SETTLE_MS)
            busy = self.busy_percent(rig, idle, SPEED_MEASURE_MS)
            self.assertEqual(rate, rig.audio.rate)
            self.assertEqual(FULL_RING_WORDS if step == 0 else STRETCH_RING_WORDS,
                             rig.audio.ring_words())
            self.assertEqual([], rig.audio.underruns(started), percent)
            self.assertEqual(0, rig.audio.output_underruns())
            self.assert_healthy(rig.session)
            loads.append(busy)
            print("\n[%s] %d%% speed: core 0 busy %.1f %%, %d Hz output" %
                  (label, percent, busy, rig.audio.rate))
        return loads

    def speech_16k(self):
        pattern = struct.pack("<8h", 0, 2828, 4000, 2828, 0, -2828, -4000, -2828)
        pcm = pattern * (16000 * 12 // 8)
        return struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                           16, 1, 1, 16000, 32000, 2, 16, b"data", len(pcm)) + pcm

    def test_playback_speed_core_0_load_for_44_khz_flac(self):
        from echo_emulator.flac_image import FlacImage
        from echo_emulator.real_volume import ALBUM_FRAMES
        self.measure_speed_load((("Album.flac", None, None, "", FlacImage(ALBUM_FRAMES)),),
                                "44.1 kHz FLAC", 44100)

    def test_playback_speed_core_0_load_for_16_khz_speech(self):
        self.measure_speed_load((("Speech.wav", self.speech_16k()),), "16 kHz speech", 32000)

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
