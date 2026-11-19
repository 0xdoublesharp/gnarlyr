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

SECONDS = 16
SETTLE_MS = 1000
DARK_SETTLE_MS = 1500
MEASURE_MS = 3000
TAP_MS = 120
FED_MS = 40
ROW_GAP_LIMIT_MS = 250
FRAME_PACER_FAST_FRAMES = 6
ROW_CYCLE_WATCH_MS = 16000
MARQUEE_PAUSE_MS = 1200
FIRST_STEP_LIMIT_MS = 250
PAUSED_DARK_MS = 3000
RECOVERY_MS = 1500
PLAY_KEY = 250
NEXT_KEY = 100
SCREEN_TIMEOUT_LIMIT_MS = 40000
BLOCKS_OFFSET = 12
SMALL_BLOCK_TITLE = "01 FLAC 96k 24-bit small blocks 3 min"
CASES = {
    "flac96_512": (SMALL_BLOCK_TITLE + ".flac", 96000,
                   ["-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24",
                    "-frame_size", "512"]),
    "flac44_long_title": ("03 A title far too long for any list row on this player, so it has to "
                          "scroll sideways.flac", 44100, ["-c:a", "flac", "-sample_fmt", "s16"]),
    "flac44_short": ("01 Short.flac", 44100, ["-c:a", "flac", "-sample_fmt", "s16"]),
    "flac44_lead": ("07 Lead.flac", 44100, ["-c:a", "flac", "-sample_fmt", "s16"], 60),
    "flac44_row": ("02 A song title just too long for a list row.flac", 44100,
                   ["-c:a", "flac", "-sample_fmt", "s16"]),
    "flac192_512": ("02 FLAC 192k small blocks.flac", 192000,
                    ["-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24",
                     "-frame_size", "512"]),
    "opus48_20ms": ("08 Opus.opus", 48000, ["-c:a", "libopus", "-frame_duration", "20",
                                            "-b:a", "160k"]),
    "opus48_2_5ms": ("08 Opus 2.5 ms.opus", 48000, ["-c:a", "libopus", "-frame_duration", "2.5",
                                                   "-b:a", "96k"]),
    "opus48_20ms_96k": ("08 Opus 96k.opus", 48000, ["-c:a", "libopus", "-frame_duration", "20",
                                                    "-b:a", "96k"]),
    "vorbis44": ("09 Vorbis.ogg", 44100, ["-c:a", "vorbis", "-strict", "-2"]),
    "flac44": ("10 FLAC 44k.flac", 44100, ["-c:a", "flac", "-sample_fmt", "s16"]),
}


def encoded(case):
    name, rate, codec = CASES[case][:3]
    seconds = CASES[case][3] if len(CASES[case]) > 3 else SECONDS
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / name
        command = ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                   "sine=frequency=440:sample_rate=%d:duration=%d" % (rate, seconds),
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

    def measure(self, rig, meter, frames, case, dark, tap=True):
        machine = rig.machine
        if dark and tap:
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

    def test_a_scrolling_title_during_96_khz_playback_is_paced_and_keeps_the_feed(self):
        rig, meter, frames = self.feed_rig("flac96_512")
        result = self.measure(rig, meter, frames, "flac96_512", False)
        self.assert_fed(result)
        self.assertLessEqual(result["frames_drawn"], MEASURE_MS * 12 // 1000, result)
        self.assertGreaterEqual(result["frames_drawn"], MEASURE_MS * 3 // 1000, result)
        self.assertLessEqual(result["core0_busy_percent"], 60, result)
        self.assert_healthy(rig.session)

    def test_a_long_selected_row_keeps_scrolling_at_the_idle_pace_after_a_key(self):
        rig, meter, frames = self.feed_rig("flac44_long_title")
        rig.press("back")
        rig.run_for(DARK_SETTLE_MS)
        pressed_ms = rig.press("next")
        result = meter.measure(rig, rig.session.card, MEASURE_MS)
        drawn = [stamp for stamp in frames if stamp >= pressed_ms]
        gaps = [later - earlier for earlier, later in zip(drawn, drawn[1:])]
        self.assertLessEqual(max(gaps, default=MEASURE_MS), ROW_GAP_LIMIT_MS, (drawn, result))
        span_ms = rig.machine.milliseconds() - pressed_ms
        self.assertLessEqual(len(drawn), span_ms * 12 // 1000 + FRAME_PACER_FAST_FRAMES, (drawn, result))
        self.assertGreaterEqual(len(drawn), span_ms * 5 // 1000, (drawn, result))
        self.assertEqual(0, result["underruns"], result)
        self.assert_healthy(rig.session)

    def test_the_measure_script_with_the_cable_in_keeps_a_96_khz_small_block_flac_fed(self):
        script = "\n".join(["build " + self.build_tag(), "play", ",".join(["down"] * 7), "play",
                            "play", "wait 1500", "measure 3000", "screen off", "wait 1500",
                            "measure 3000", "screen on", "reconnect", ""])
        name, data = encoded("flac96_512")
        volume = Fat32Volume()
        volume.add_file(name, data)
        volume = self.debug_volume(volume, run_txt=script.encode("ascii"))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume,
                  module_elfs=self.module_elfs)
        DecoderLoad(rig.audio, self.elf.parent)
        session, machine = rig.session, rig.machine
        for name in ("usb_session_storage_changed", "debug_script_take", "hal_system_usb_reconnect"):
            machine.note(name)
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        self.assertEqual(report["values"]["script.state"], "finished", report["script"])
        measures = report["script"]["measures"]
        print(json.dumps(measures))
        destination = os.environ.get("ECHO_FEED_REPORT")
        if destination:
            with open(destination, "a", encoding="utf-8") as output:
                output.write(json.dumps({"case": "script", "measures": measures}) + "\n")
        lit, dark = measures
        self.assertEqual(96000, rig.audio.rate)
        for measure in (lit, dark):
            self.assertEqual(0, measure["underruns"], measures)
            self.assertGreaterEqual(measure["buffered_ms"], FED_MS, measures)
        self.assertLessEqual(lit["frames"], MEASURE_MS * 12 // 1000, measures)
        self.assertEqual(0, dark["frames"], measures)
        self.assert_healthy(session)

    def test_every_marquee_cycle_of_a_long_row_steps_on_time_after_its_pause(self):
        from unicorn.arm_const import UC_ARM_REG_R2
        volume = Fat32Volume()
        for case in ("flac44_short", "flac44_row"):
            volume.add_file(*encoded(case))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume,
                  module_elfs=self.module_elfs)
        machine = rig.machine
        changed = []
        machine.hook_symbol("frame_pacer_done", lambda: changed.append(machine.milliseconds())
                            if machine.reg(UC_ARM_REG_R2) & 0xFF else None)
        rig.open_songs()
        pressed_ms = rig.press("next")
        rig.run_for(ROW_CYCLE_WATCH_MS)
        moved = [stamp for stamp in changed if stamp >= pressed_ms]
        gaps = [later - earlier for earlier, later in zip(moved, moved[1:])]
        print(json.dumps({"changed": moved, "gaps": gaps}))
        pauses = [gap for gap in gaps if gap >= MARQUEE_PAUSE_MS]
        self.assertGreaterEqual(len(pauses), 1, gaps)
        self.assertLessEqual(max(gaps), MARQUEE_PAUSE_MS + FIRST_STEP_LIMIT_MS, gaps)
        self.assert_healthy(rig.session)

    def tap_power(self, rig):
        start = rig.machine.milliseconds() + 20
        rig.machine.hold_power(start, TAP_MS)
        rig.machine.run(until_ms=start + TAP_MS + 50)

    def resumed_in_the_dark(self, case):
        rig, meter, frames = self.feed_rig(case)
        rig.press("play")
        self.tap_power(rig)
        rig.run_for(PAUSED_DARK_MS)
        resumed_ms = rig.machine.milliseconds()
        rig.machine.press_key(PLAY_KEY, delay_ms=10, hold_ms=TAP_MS)
        rig.run_for(RECOVERY_MS)
        dark = self.measure(rig, meter, frames, case, True, tap=False)
        self.tap_power(rig)
        rig.run_for(RECOVERY_MS)
        lit = self.measure(rig, meter, frames, case, False)
        self.assertTrue(dark["panel_asleep"], dark)
        self.assertFalse(lit["panel_asleep"], lit)
        self.assertEqual([], rig.audio.underruns(resumed_ms)[:5], case)
        return dark, lit

    def test_playback_resumed_with_the_screen_off_after_a_pause_stays_fed(self):
        for case in ("opus48_20ms_96k", "opus48_2_5ms", "flac96_512", "vorbis44"):
            with self.subTest(case=case):
                for result in self.resumed_in_the_dark(case):
                    self.assert_fed(result)

    def panel_asleep(self, rig):
        return bool(rig.machine.read_memory(rig.machine.symbols["panel_asleep"], 1)[0])

    def wait_for_the_screen_timeout(self, rig):
        machine = rig.machine
        machine.run(until=lambda: self.panel_asleep(rig),
                    until_ms=machine.milliseconds() + SCREEN_TIMEOUT_LIMIT_MS)
        self.assertTrue(self.panel_asleep(rig))

    def dark_then_lit(self, rig, meter, case, since_ms):
        dark = self.measure(rig, meter, [], case, True, tap=False)
        self.tap_power(rig)
        rig.run_for(RECOVERY_MS)
        lit = self.measure(rig, meter, [], case, False)
        self.assertTrue(dark["panel_asleep"], dark)
        self.assertFalse(lit["panel_asleep"], lit)
        for result in (dark, lit):
            self.assert_fed(result)
        self.assertEqual([], rig.audio.underruns(since_ms)[:5], case)

    def test_opus_started_after_the_screen_timeout_stays_fed_after_the_screen_wakes(self):
        case = "opus48_20ms_96k"
        rig, meter, frames = self.feed_rig(case)
        rig.press("play")
        self.wait_for_the_screen_timeout(rig)
        rig.run_for(PAUSED_DARK_MS)
        resumed_ms = rig.machine.milliseconds()
        rig.machine.press_key(PLAY_KEY, delay_ms=10, hold_ms=TAP_MS)
        rig.run_for(RECOVERY_MS)
        self.dark_then_lit(rig, meter, case, resumed_ms)
        volume = Fat32Volume()
        for name in ("flac44_lead", case):
            volume.add_file(*encoded(name))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume,
                  module_elfs=self.module_elfs)
        DecoderLoad(rig.audio, self.elf.parent)
        meter = PowerMeter(rig.machine)
        rig.open_songs()
        rig.play_song(1)
        self.wait_for_the_screen_timeout(rig)
        rig.run_for(PAUSED_DARK_MS)
        skipped_ms = rig.machine.milliseconds()
        released = rig.machine.press_ladder([(PLAY_KEY, TAP_MS), (NEXT_KEY, TAP_MS),
                                             (PLAY_KEY, TAP_MS)])
        rig.machine.run(until_ms=released + RECOVERY_MS)
        self.assertEqual(48000, rig.audio.rate)
        self.dark_then_lit(rig, meter, case, skipped_ms)
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
