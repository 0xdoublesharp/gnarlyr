import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main
from echo_emulator.fat_volume import Fat32Volume
from echo_emulator.playback import Rig
from echo_emulator.power import DecoderLoad, PowerMeter

SECONDS = 9
SETTLE_MS = 1000
DARK_SETTLE_MS = 1500
MEASURE_MS = 3000
TAP_MS = 120
FED_MS = 40
BLOCKS_OFFSET = 12
SMALL_BLOCK_TITLE = "01 FLAC 96k 24-bit small blocks 3 min"
CASES = {
    "flac96_512": (SMALL_BLOCK_TITLE + ".flac", 96000,
                   ["-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24",
                    "-frame_size", "512"]),
    "flac192_512": ("02 FLAC 192k small blocks.flac", 192000,
                    ["-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24",
                     "-frame_size", "512"]),
    "opus48_20ms": ("08 Opus.opus", 48000, ["-c:a", "libopus", "-frame_duration", "20",
                                            "-b:a", "160k"]),
    "vorbis44": ("09 Vorbis.ogg", 44100, ["-c:a", "vorbis", "-strict", "-2"]),
    "flac44": ("10 FLAC 44k.flac", 44100, ["-c:a", "flac", "-sample_fmt", "s16"]),
}


def encoded(case):
    name, rate, codec = CASES[case]
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / name
        command = ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                   "sine=frequency=440:sample_rate=%d:duration=%d" % (rate, SECONDS),
                   "-af", "volume=0.1", "-ac", "2", "-ar", str(rate), "-map_metadata", "-1",
                   "-fflags", "+bitexact"] + codec + [str(path)]
        subprocess.run(command, check=True)
        return name, path.read_bytes()


class EchoImageTest(EchoImageHarness):
    def feed_rig(self, case):
        name, data = encoded(case)
        volume = Fat32Volume()
        volume.add_file(name, data)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume,
                  module_elfs=self.module_elfs)
        DecoderLoad(rig.audio, self.elf.parent)
        meter = PowerMeter(rig.machine)
        frames = []
        rig.machine.hook_symbol("frame_pacer_done",
                                lambda: frames.append(rig.machine.milliseconds()))
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        self.assertEqual(CASES[case][1], rig.audio.rate)
        return rig, meter, frames

    def blocks(self, rig):
        stats = rig.machine.symbols["stats"]
        return struct.unpack("<I", rig.machine.read_memory(stats + BLOCKS_OFFSET, 4))[0]

    def measure(self, rig, meter, frames, case, dark):
        machine = rig.machine
        if dark:
            start = machine.milliseconds() + 20
            machine.hold_power(start, TAP_MS)
            machine.run(until_ms=start + TAP_MS + 50)
        rig.run_for(DARK_SETTLE_MS)
        first_ms, first_blocks = machine.milliseconds(), self.blocks(rig)
        result = meter.measure(rig, rig.session.card, MEASURE_MS)
        level = rig.audio.levels[-1][1] if rig.audio.levels else 0
        result.update(
            case=case, dark=dark,
            blocks=self.blocks(rig) - first_blocks,
            buffered_ms=round(max(0, level) * 1000 / rig.audio.rate),
            frames_drawn=len([stamp for stamp in frames if stamp >= first_ms]))
        print(json.dumps({key: result[key] for key in (
            "case", "dark", "blocks", "buffered_ms", "underruns",
            "output_underruns", "frames_drawn", "core0_busy_percent", "core0_wfi_wakes_per_s",
            "core0_passes_per_s", "core1_busy_percent", "core1_wakes_per_s", "core1_clock_ms")}))
        destination = os.environ.get("ECHO_FEED_REPORT")
        if destination:
            with open(destination, "a", encoding="utf-8") as report:
                report.write(json.dumps(result) + "\n")
        return result

    def assert_fed(self, result):
        self.assertEqual(0, result["underruns"], result)
        self.assertEqual(0, result["output_underruns"], result)
        self.assertGreaterEqual(result["buffered_ms"], FED_MS, result)

    def test_a_96_khz_flac_with_512_sample_blocks_stays_fed_with_the_screen_off(self):
        rig, meter, frames = self.feed_rig("flac96_512")
        result = self.measure(rig, meter, frames, "flac96_512", True)
        self.assertEqual(0, result["frames_drawn"], result)
        self.assert_fed(result)
        self.assertLessEqual(result["core0_busy_percent"], 25, result)
        self.assert_healthy(rig.session)

    def test_192_khz_flac_opus_vorbis_and_44_khz_flac_stay_fed_with_the_screen_off(self):
        for case in ("flac192_512", "opus48_20ms", "vorbis44", "flac44"):
            with self.subTest(case=case):
                rig, meter, frames = self.feed_rig(case)
                result = self.measure(rig, meter, frames, case, True)
                self.assert_fed(result)
                self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
