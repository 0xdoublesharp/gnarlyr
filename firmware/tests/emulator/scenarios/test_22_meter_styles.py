import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, struct, IDENTITY, fnv1a
from echo_emulator.flac_image import FlacImage, BLOCK_SAMPLES

VU, SPECTRUM = 2, 3
VU_CONVERTED_BUDGET_AFTER_USB_DAC_STATS = 3.5
CORE0_BUDGETS = {(VU, "flac44"): 14.08, (SPECTRUM, "flac44"): 13.63,
                 (VU, "flac96_512"): 16.70, (SPECTRUM, "flac96_512"): 18.22}
MUSIC_SECONDS = 16
MUSIC = ("0.35*sin(2*PI*55*t)*(0.4+0.6*lt(mod(t,0.5),0.12))"
         "+0.18*sin(2*PI*110*t+sin(3*t))"
         "+0.12*sin(2*PI*660*t)*(0.5+0.5*sin(2*PI*0.7*t))"
         "+0.06*sin(2*PI*(2500+900*sin(t))*t)"
         "+0.05*(random(0)-0.5)*lt(mod(t,0.25),0.06)")


def music_flac_96k():
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "01 Music 96k.flac"
        right = MUSIC.replace("660", "520")
        subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                        "aevalsrc=exprs='%s|%s':s=96000:d=%d" % (MUSIC, right, MUSIC_SECONDS),
                        "-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24",
                        "-frame_size", "512", "-map_metadata", "-1", "-fflags", "+bitexact",
                        str(path)], check=True)
        return path.name, path.read_bytes()


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
    def styled_volume(self, volume, style):
        settings = bytearray(self.saved_settings(4, 15, shuffle=0))
        settings[7] = 6
        settings[51] = (style & 1) << 5
        settings[52] = (style >> 1) << 7
        checksum = fnv1a(settings[:54])
        settings[54:58] = struct.pack("<I", checksum)
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("settings.bin", settings, directory=IDENTITY.data_directory)
        return volume

    def tick_timer(self, machine):
        state = {"entry": None, "start": None, "durations": []}

        def entered():
            state["entry"] = machine.microseconds()

        def sampled():
            state["start"] = state["entry"]

        def presented():
            if state["start"] is not None:
                state["durations"].append(machine.microseconds() - state["start"])
                state["start"] = None

        machine.hook_symbol("meter_now_playing_animate", entered)
        machine.hook_symbol("hal_audio_visual_buffer", sampled)
        machine.hook_symbol("hal_display_present", presented)
        return state

    def report(self, result, ticks, pixels):
        result.update(ticks=len(ticks), tick_mean_us=round(sum(ticks) / max(1, len(ticks))),
                      tick_max_us=max(ticks, default=0),
                      pixels_per_tick=round(pixels / max(1, len(ticks))))
        print("\n" + json.dumps(result))
        destination = os.environ.get("ECHO_METER_REPORT")
        if destination:
            with open(destination, "a", encoding="utf-8") as handle:
                handle.write(json.dumps(result) + "\n")

    def visual_rig(self, style, converted):
        from echo_emulator.playback import Rig
        from echo_emulator.fat_volume import Fat32Volume
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
        self.styled_volume(volume, style)
        return Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                   volume=volume, module_elfs=self.module_elfs)

    def measure_style(self, style, converted, budget=3):
        rig = self.visual_rig(style, converted)
        machine = rig.machine
        idle = self.idle_counter(machine)
        timer = self.tick_timer(machine)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        _, stage = self.module(machine, "stage")
        self.assertTrue(stage.loaded(machine))
        rig.run_for(300)
        reads = machine.trace("hal_audio_visual_buffer")
        since_ms = machine.milliseconds()
        since = machine.cycles()
        timer["durations"].clear()
        animated = self.busy_percent(rig, idle, 3000)
        ticks = list(timer["durations"])
        windows = [window for window in machine.panel.windows if window.cycles >= since and window.width < 300]
        self.assertGreater(len(windows), 0)
        frames = len([stamp for stamp in reads if stamp >= since_ms])
        self.assertGreaterEqual(frames, 29)
        self.assertLessEqual(frames, 31)
        machine.stub_symbol("meter_now_playing_animate", lambda machine: 0)
        rig.run_for(200)
        still = self.busy_percent(rig, idle, 3000)
        if not converted:
            self.report({"case": "flac44", "style": "vu" if style == VU else "spectrum",
                         "animated": round(animated, 2), "still": round(still, 2),
                         "increase": round(animated - still, 2)},
                        ticks, sum(window.pixels for window in windows))
            self.assertLessEqual(animated, CORE0_BUDGETS[(style, "flac44")])
        self.assertLessEqual(animated - still, budget)
        self.assertEqual([], rig.audio.underruns(started))
        self.assertEqual(0, rig.audio.output_underruns())
        self.assertEqual(32000 if converted else 44100, rig.audio.rate)
        self.assert_healthy(rig.session)
        print("\n[%s %s] core 0 animated %.2f %%, still %.2f %%, increase %.2f points, no underruns" %
              ("VU" if style == 2 else "Spectrum", "16 to 32 kHz" if converted else "44.1 kHz FLAC",
               animated, still, animated - still))

    def measure_96k(self, style):
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        from echo_emulator.power import DecoderLoad, PowerMeter
        name, data = music_flac_96k()
        volume = Fat32Volume()
        volume.add_file(name, data)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=self.styled_volume(volume, style), module_elfs=self.module_elfs)
        DecoderLoad(rig.audio, self.elf.parent)
        meter = PowerMeter(rig.machine)
        machine = rig.machine
        timer = self.tick_timer(machine)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(1500)
        self.assertEqual(96000, rig.audio.rate)
        since = machine.cycles()
        timer["durations"].clear()
        result = meter.measure(rig, rig.session.card, 3000)
        ticks = list(timer["durations"])
        pixels = sum(window.pixels for window in machine.panel.windows
                     if window.cycles >= since and window.width < 300)
        machine.stub_symbol("meter_now_playing_animate", lambda machine: 0)
        rig.run_for(200)
        still = meter.measure(rig, rig.session.card, 3000)
        busy = result["core0_busy_percent"]
        self.report({"case": "flac96_512", "style": "vu" if style == VU else "spectrum",
                     "animated": round(busy, 2), "still": round(still["core0_busy_percent"], 2),
                     "increase": round(busy - still["core0_busy_percent"], 2),
                     "underruns": result["underruns"],
                     "output_underruns": result["output_underruns"]}, ticks, pixels)
        self.assertGreaterEqual(len(ticks), 29)
        self.assertEqual(0, result["underruns"], result)
        self.assertEqual(0, result["output_underruns"], result)
        self.assertLessEqual(busy, CORE0_BUDGETS[(style, "flac96_512")])
        self.assert_healthy(rig.session)

    def test_vu_style_keeps_a_96_khz_24_bit_small_block_flac_fed_with_the_screen_on(self):
        self.measure_96k(VU)

    def test_spectrum_style_keeps_a_96_khz_24_bit_small_block_flac_fed_with_the_screen_on(self):
        self.measure_96k(SPECTRUM)

    def test_vu_style_leaves_core_0_room_with_44_khz_flac(self):
        self.measure_style(2, False)

    def test_vu_style_leaves_core_0_room_with_converted_16_khz(self):
        self.measure_style(2, True, budget=VU_CONVERTED_BUDGET_AFTER_USB_DAC_STATS)

    def test_spectrum_style_leaves_core_0_room_with_44_khz_flac(self):
        self.measure_style(3, False)

    def test_spectrum_style_leaves_core_0_room_with_converted_16_khz(self):
        self.measure_style(3, True)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
