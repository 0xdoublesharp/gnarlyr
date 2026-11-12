import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, struct

ALL_EFFECTS = 3 | 1 << 8 | 1 << 16
MEASURE_MS = 2000
EFFECTS_LOAD_LIMIT_PERCENT = 15


class EchoImageTest(EchoImageHarness):
    def paused_speech_rig(self, rate):
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        frames = bytearray()
        for index in range(rate * 12):
            spoken = index % (3 * rate) < rate
            left = (index % 40 - 20) * 400 if spoken else 0
            frames += struct.pack("<hh", left, -left // 2)
        wav = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(frames) + 36, b"WAVE", b"fmt ",
                          16, 1, 2, rate, rate * 4, 4, 16, b"data", len(frames)) + bytes(frames)
        volume = Fat32Volume()
        volume.add_file("Spoken.wav", wav)
        return Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                   volume=volume, module_elfs=self.module_elfs)

    def count_effects(self, machine):
        from unicorn import UC_HOOK_CODE
        _, effects = self.module(machine, "effects")
        names = ("effects_module_", "crossfeed", "boost", "skip")
        chosen = sorted(address for name, address in effects.symbols.items()
                        if any(part in name for part in names))
        after = min(address for address in effects.symbols.values() if address > chosen[-1])
        counted = [0, False]

        def count(uc, address, size, user):
            counted[0] += counted[1]

        machine.uc.hook_add(UC_HOOK_CODE, count, None, chosen[0], after - 1)
        return counted

    def effects_load(self, rig, label, rate):
        machine = rig.machine
        chosen = [0]
        machine.stub_symbol("listening_effects_choose", lambda machine: chosen[0])
        idle = self.idle_counter(machine)
        counted = self.count_effects(machine)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        rig.run_for(500)
        self.assertEqual(rate, rig.audio.rate)
        plain = self.busy_percent(rig, idle, MEASURE_MS)
        chosen[0] = ALL_EFFECTS
        rig.run_for(200)
        counted[1] = True
        effected = self.busy_percent(rig, idle, MEASURE_MS)
        self.assertEqual([], rig.audio.underruns(started))
        self.assert_healthy(rig.session)
        counted[1] = False
        per_frame = counted[0] / (rate * MEASURE_MS / 1000)
        print("\n[%s] core 0 busy %.1f %% without effects, %.1f %% with crossfeed, skip silence "
              "and speech boost; %.1f effect instructions per output frame"
              % (label, plain, effected, per_frame))
        self.assertGreater(counted[0], 0)
        self.assertLess(effected - plain, EFFECTS_LOAD_LIMIT_PERCENT)

    def test_all_effects_play_44_khz_music_without_underruns(self):
        self.effects_load(self.playback_rig(), "effects, 44.1 kHz FLAC", 44100)

    def test_all_effects_play_converted_speech_without_underruns(self):
        self.effects_load(self.paused_speech_rig(16000), "effects, 16 kHz WAV at 32 kHz", 32000)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
