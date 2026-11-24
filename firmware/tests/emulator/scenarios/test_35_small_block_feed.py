import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import CONFIG, EchoImageHarness, main
from echo_emulator.fat_volume import Fat32Volume
from echo_emulator.playback import Rig
from echo_emulator.core1 import CONTROL
from echo_emulator.power import DecoderLoad, PowerMeter

SECONDS = 16
SETTLE_MS = 1000
DARK_SETTLE_MS = 1500
MEASURE_MS = 3000
TAP_MS = 120
FED_MS = 40
PHASE_DELAYS_0_TO_12_MS = tuple(range(0, 13, 4))
PHASE_DELAYS_16_TO_28_MS = tuple(range(16, 29, 4))
PHASE_DELAYS_32_TO_40_MS = tuple(range(32, 41, 4))
PHASE_DELAYS_32_TO_44_MS = tuple(range(32, 45, 4))
PHASE_DELAYS_48_TO_64_MS = tuple(range(48, 65, 4))
PHASE_DELAYS_68_TO_80_MS = tuple(range(68, 81, 4))
PHASE_DELAYS_84_TO_96_MS = tuple(range(84, 97, 4))
PHASE_DELAYS_100_TO_112_MS = tuple(range(100, 113, 4))
PHASE_DELAYS_116_TO_128_MS = tuple(range(116, 129, 4))
DELAY_STEP_MS = 4
WINDOW_SHIFTS_MS = (0, 5, 11)
RING_FRAMES = 14000
ROW_GAP_LIMIT_MS = 250
FRAME_PACER_FAST_FRAMES = 6
MARQUEE_STEP_MS = 100
MARQUEE_STEP_SLACK_MS = 15
POSITION_FRAME_CLOSE_MS = 60
ROW_CYCLE_WATCH_MS = 16000
MARQUEE_PAUSE_MS = 1200
FIRST_STEP_LIMIT_MS = 250
PAUSED_DARK_MS = 3000
RECOVERY_MS = 1500
PLAY_KEY = 250
LOWEST_CORE1_MHZ = 25
TRACK_CHANGE_WATCH_MS = 300
TRACK_CHANGE_LIMIT_MS = 2000
TRACK_CHANGE_SLOW_LIMIT_US = 50000
DSD128_RATE = 5644800
DSD_OUTPUT_RATE = 88200
DSD_PATTERN_FRAMES = 22050
NEXT_KEY = 100
MENU_KEY = 350
PREVIEW_LEAD_MS = 800
PREVIEW_TAIL_MS = 400
SCREEN_TIMEOUT_LIMIT_MS = 40000
BLOCKS_OFFSET = 12
CLOCK_CHANGE_MHZ = 96
AUDIO_CLOCK_MHZ = 200
CLOCK_CALL_LIMIT_MS = 3000
CLOCK_SEEK_TOLERANCE_MS = 100
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
    "flac176_512": ("02 FLAC 176k small blocks.flac", 176400,
                    ["-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24",
                     "-frame_size", "512"]),
    "flac192_512": ("02 FLAC 192k small blocks.flac", 192000,
                    ["-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24",
                     "-frame_size", "512"]),
    "opus48_20ms": ("08 Opus.opus", 48000, ["-c:a", "libopus", "-frame_duration", "20",
                                            "-b:a", "160k"]),
    "opus48_2_5ms": ("08 Opus 2.5 ms.opus", 48000, ["-c:a", "libopus", "-frame_duration", "2.5",
                                                   "-b:a", "96k"]),
    "opus48_20ms_96k": ("08 Opus 96k.opus", 48000, ["-c:a", "libopus", "-frame_duration", "20",
                                                    "-b:a", "96k"]),
    "opus48_20ms_96k_next": ("09 Opus 96k next.opus", 48000, ["-c:a", "libopus",
                                                            "-frame_duration", "20",
                                                            "-b:a", "96k"]),
    "dsd128_stereo": ("05 DSD128.dsf", 88200, None),
    "vorbis44": ("09 Vorbis.ogg", 44100, ["-c:a", "vorbis", "-strict", "-2"]),
    "flac44": ("10 FLAC 44k.flac", 44100, ["-c:a", "flac", "-sample_fmt", "s16"]),
}


def dsd128_stereo(seconds):
    from dsd_fixture import dsf, signal
    data, samples = signal(DSD128_RATE, DSD_PATTERN_FRAMES)
    repeats = seconds * DSD_OUTPUT_RATE // DSD_PATTERN_FRAMES
    return dsf([channel * repeats for channel in data], DSD128_RATE, samples * repeats)


ENCODED = {}


def fixture_cache_path(case):
    payload = json.dumps([case, CASES[case], SECONDS], sort_keys=True).encode("utf-8")
    digest = hashlib.sha256(payload).hexdigest()[:24]
    return Path(CONFIG["image"]).parent / "fixture_cache" / f"{case}-{digest}.bin"


def encoded(case):
    if case in ENCODED:
        return ENCODED[case]
    name = CASES[case][0]
    cached = fixture_cache_path(case)
    try:
        data = cached.read_bytes()
    except FileNotFoundError:
        _, data = encode(case)
        cached.parent.mkdir(parents=True, exist_ok=True)
        fd, temp_name = tempfile.mkstemp(prefix=f".{case}-", suffix=".tmp", dir=cached.parent)
        temp_path = Path(temp_name)
        try:
            with os.fdopen(fd, "wb") as handle:
                handle.write(data)
            os.replace(temp_path, cached)
        except BaseException:
            temp_path.unlink(missing_ok=True)
            raise
    ENCODED[case] = (name, data)
    return ENCODED[case]


def encode(case):
    name, rate, codec = CASES[case][:3]
    seconds = CASES[case][3] if len(CASES[case]) > 3 else SECONDS
    if codec is None:
        return name, dsd128_stereo(seconds)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / name
        command = ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                   "sine=frequency=440:sample_rate=%d:duration=%d" % (rate, seconds),
                   "-af", "volume=0.1", "-ac", "2", "-ar", str(rate), "-map_metadata", "-1",
                   "-fflags", "+bitexact"] + codec + [str(path)]
        subprocess.run(command, check=True)
        return name, path.read_bytes()


def position_frames(drawn):
    found = []
    for index in range(1, len(drawn)):
        before = drawn[index - 1]
        after = drawn[index + 1] if index + 1 < len(drawn) else None
        if after is None:
            if drawn[index] - before < POSITION_FRAME_CLOSE_MS:
                found.append(drawn[index])
        elif (abs(after - before - MARQUEE_STEP_MS) <= MARQUEE_STEP_SLACK_MS
              and drawn[index] - before < MARQUEE_STEP_MS - MARQUEE_STEP_SLACK_MS):
            found.append(drawn[index])
    return found


class EchoImageTest(EchoImageHarness):
    def feed_rig(self, case, start_delay_ms=0):
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
        rig.run_for(start_delay_ms)
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        self.assertEqual(CASES[case][1], rig.audio.rate)
        return rig, meter, frames

    def blocks(self, rig):
        stats = rig.machine.symbols["stats"]
        return struct.unpack("<I", rig.machine.read_memory(stats + BLOCKS_OFFSET, 4))[0]

    def measure(self, rig, meter, frames, case, dark, tap=True, settle_ms=DARK_SETTLE_MS):
        machine = rig.machine
        if dark and tap:
            start = machine.milliseconds() + 20
            machine.hold_power(start, TAP_MS)
            machine.run(until_ms=start + TAP_MS + 50)
        rig.run_for(settle_ms)
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

    def test_a_menu_scrub_preview_during_96_khz_playback_redraws_at_most_ten_times_a_second(self):
        rig, meter, frames = self.feed_rig("flac96_512")
        machine = rig.machine
        rig.run_for(DARK_SETTLE_MS)
        hold_ms = PREVIEW_LEAD_MS + MEASURE_MS + PREVIEW_TAIL_MS
        start = machine.milliseconds() + 20
        machine.press_ladder([(MENU_KEY, TAP_MS), (NEXT_KEY, hold_ms), (MENU_KEY, TAP_MS)])
        machine.run(until_ms=start + TAP_MS + PREVIEW_LEAD_MS)
        first_ms = machine.milliseconds()
        result = meter.measure(rig, rig.session.card, MEASURE_MS)
        drawn = len([stamp for stamp in frames if first_ms <= stamp < first_ms + MEASURE_MS])
        print(json.dumps({"case": "flac96_512 preview", "frames_drawn": drawn,
                          "underruns": result["underruns"],
                          "output_underruns": result["output_underruns"],
                          "core0_busy_percent": result["core0_busy_percent"]}))
        self.assertEqual(0, result["underruns"], result)
        self.assertEqual(0, result["output_underruns"], result)
        self.assertLessEqual(drawn, MEASURE_MS * 12 // 1000, result)
        self.assertGreaterEqual(drawn, MEASURE_MS * 8 // 1000, result)
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
        position = position_frames(drawn)
        self.assertLessEqual(len(drawn) - len(position),
                             span_ms * 12 // 1000 + FRAME_PACER_FAST_FRAMES, (drawn, position, result))
        self.assertLessEqual(len(position), -(-span_ms // 1000), (drawn, position, result))
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

    def resumed_in_the_dark(self, case, paused_ms=PAUSED_DARK_MS):
        rig, meter, frames = self.feed_rig(case)
        rig.press("play")
        self.tap_power(rig)
        rig.run_for(paused_ms)
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

    def call_clock_change(self, machine, mhz):
        from echo_emulator.machine import TRAMPOLINE
        from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0
        returned = TRAMPOLINE + 0x904
        machine.run(until=lambda: machine.exception_depth == 0,
                    until_ms=machine.milliseconds() + CLOCK_CALL_LIMIT_MS)
        self.assertEqual(0, machine.exception_depth)
        machine.add_address_hook(returned, lambda: machine.stop("clock change returned"))
        context = machine.uc.context_save()
        machine.uc.reg_write(UC_ARM_REG_R0, mhz)
        machine.uc.reg_write(UC_ARM_REG_LR, returned | 1)
        machine.uc.reg_write(UC_ARM_REG_PC, machine.symbols["hal_system_set_clock_mhz"] | 1)
        stopped = machine.run(until_ms=machine.milliseconds() + CLOCK_CALL_LIMIT_MS)
        switched = machine.reg(UC_ARM_REG_R0)
        machine.uc.context_restore(context)
        self.assertEqual("clock change returned", stopped)
        return switched

    def test_a_clock_change_during_core_1_playback_resumes_at_the_played_position_and_stays_fed(self):
        from unicorn.arm_const import UC_ARM_REG_R2
        rig, meter, frames = self.feed_rig("flac96_512")
        machine = rig.machine
        seeks, starts = [], []
        machine.hook_symbol("audio_engine_seek_on", lambda: seeks.append(machine.reg(UC_ARM_REG_R2)))
        machine.hook_symbol("board_core1_start_stream", lambda: starts.append(machine.milliseconds()))
        played_ms = rig.audio.elapsed_us(machine) // 1000
        changed_ms = machine.milliseconds()
        logged = len(machine.clock_log)
        self.assertEqual(1, self.call_clock_change(machine, CLOCK_CHANGE_MHZ))
        rates = [hz // 1000000 for _, hz in machine.clock_log[logged:]]
        print(json.dumps({"played_ms": played_ms, "seeks": seeks, "starts": starts, "rates": rates}))
        self.assertIn(CLOCK_CHANGE_MHZ, rates)
        self.assertEqual(AUDIO_CLOCK_MHZ, rates[-1])
        self.assertLess(rates.index(CLOCK_CHANGE_MHZ), len(rates) - 1)
        self.assertEqual(1, len(seeks), seeks)
        self.assertEqual(1, len(starts), starts)
        self.assertLessEqual(abs(seeks[0] - played_ms), CLOCK_SEEK_TOLERANCE_MS, (seeks, played_ms))
        rig.run_for(RECOVERY_MS)
        result = self.measure(rig, meter, frames, "flac96_512 clock change", False)
        self.assert_fed(result)
        self.assertEqual([], rig.audio.underruns(changed_ms)[:5])
        self.assert_healthy(rig.session)

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

    def assert_resumed_in_load_window(self, case, offset_ms):
        for result in self.resumed_in_the_dark(case, PAUSED_DARK_MS + offset_ms):
            self.assert_fed(result)

    def test_dsd128_resume_at_0_ms_in_core_1s_load_window_stays_fed(self):
        self.assert_resumed_in_load_window("dsd128_stereo", 0)

    def test_dsd128_resume_at_125_ms_in_core_1s_load_window_stays_fed(self):
        self.assert_resumed_in_load_window("dsd128_stereo", 125)

    def test_dsd128_resume_at_250_ms_in_core_1s_load_window_stays_fed(self):
        self.assert_resumed_in_load_window("dsd128_stereo", 250)

    def test_dsd128_resume_at_375_ms_in_core_1s_load_window_stays_fed(self):
        self.assert_resumed_in_load_window("dsd128_stereo", 375)

    def test_opus_2_5ms_resume_at_0_ms_in_core_1s_load_window_stays_fed(self):
        self.assert_resumed_in_load_window("opus48_2_5ms", 0)

    def test_opus_2_5ms_resume_at_125_ms_in_core_1s_load_window_stays_fed(self):
        self.assert_resumed_in_load_window("opus48_2_5ms", 125)

    def test_opus_2_5ms_resume_at_250_ms_in_core_1s_load_window_stays_fed(self):
        self.assert_resumed_in_load_window("opus48_2_5ms", 250)

    def test_opus_2_5ms_resume_at_375_ms_in_core_1s_load_window_stays_fed(self):
        self.assert_resumed_in_load_window("opus48_2_5ms", 375)

    def test_a_track_change_in_the_same_core_1_program_starts_at_its_full_clock(self):
        volume = Fat32Volume()
        for case in ("opus48_20ms_96k", "opus48_20ms_96k_next"):
            volume.add_file(*encoded(case))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume,
                  module_elfs=self.module_elfs)
        decoder = DecoderLoad(rig.audio, self.elf.parent)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        rig.press("play")
        rig.run_for(PAUSED_DARK_MS)
        self.assertEqual(LOWEST_CORE1_MHZ, decoder.mhz)
        generation = decoder.core.word(CONTROL)
        released = rig.machine.press_ladder([(PLAY_KEY, TAP_MS), (NEXT_KEY, TAP_MS),
                                             (PLAY_KEY, TAP_MS)])
        rig.machine.run(until=lambda: decoder.core.word(CONTROL) != generation,
                        until_ms=released + TRACK_CHANGE_LIMIT_MS)
        self.assertNotEqual(generation, decoder.core.word(CONTROL))
        before = dict(decoder.clock_us)
        rig.run_for(TRACK_CHANGE_WATCH_MS)
        slow_us = decoder.clock_us[LOWEST_CORE1_MHZ] - before.get(LOWEST_CORE1_MHZ, 0)
        self.assertLess(slow_us, TRACK_CHANGE_SLOW_LIMIT_US, dict(decoder.clock_us))
        self.assert_healthy(rig.session)

    def sweep_start_phases(self, case, dark, delays):
        for delay_ms in delays:
            with self.subTest(case=case, dark=dark, delay_ms=delay_ms):
                rig, meter, frames = self.feed_rig(case, delay_ms)
                since_ms = rig.machine.milliseconds()
                shift_ms = WINDOW_SHIFTS_MS[delay_ms // DELAY_STEP_MS % len(WINDOW_SHIFTS_MS)]
                result = self.measure(rig, meter, frames, case, dark,
                                      settle_ms=DARK_SETTLE_MS + shift_ms)
                window_ms = rig.machine.milliseconds() - MEASURE_MS
                lowest = min(level for stamp, level in rig.audio.levels if stamp >= window_ms)
                print(json.dumps({"case": case, "dark": dark, "delay_ms": delay_ms,
                                  "shift_ms": shift_ms,
                                  "lowest_ms": round(lowest * 1000 / rig.audio.rate, 1)}))
                self.assertEqual(0, result["underruns"], result)
                self.assertEqual(0, result["output_underruns"], result)
                self.assertGreaterEqual(lowest, RING_FRAMES // 4, (delay_ms, shift_ms))
                self.assertEqual([], rig.audio.underruns(since_ms)[:5], (delay_ms, shift_ms))

    def test_192_khz_small_block_flac_stays_fed_at_start_phases_0_to_12_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac192_512", True, PHASE_DELAYS_0_TO_12_MS)

    def test_192_khz_small_block_flac_stays_fed_at_start_phases_16_to_28_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac192_512", True, PHASE_DELAYS_16_TO_28_MS)

    def test_192_khz_small_block_flac_stays_fed_at_start_phases_32_to_44_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac192_512", True, PHASE_DELAYS_32_TO_44_MS)

    def test_192_khz_small_block_flac_stays_fed_at_start_phases_48_to_64_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac192_512", True, PHASE_DELAYS_48_TO_64_MS)

    def test_192_khz_small_block_flac_stays_fed_at_start_phases_68_to_80_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac192_512", True, PHASE_DELAYS_68_TO_80_MS)

    def test_192_khz_small_block_flac_stays_fed_at_start_phases_84_to_96_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac192_512", True, PHASE_DELAYS_84_TO_96_MS)

    def test_192_khz_small_block_flac_stays_fed_at_start_phases_100_to_112_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac192_512", True, PHASE_DELAYS_100_TO_112_MS)

    def test_192_khz_small_block_flac_stays_fed_at_start_phases_116_to_128_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac192_512", True, PHASE_DELAYS_116_TO_128_MS)

    def test_192_khz_small_block_flac_stays_fed_at_start_phases_0_to_12_ms_with_the_screen_on(self):
        self.sweep_start_phases("flac192_512", False, PHASE_DELAYS_0_TO_12_MS)

    def test_192_khz_small_block_flac_stays_fed_at_start_phases_16_to_28_ms_with_the_screen_on(self):
        self.sweep_start_phases("flac192_512", False, PHASE_DELAYS_16_TO_28_MS)

    def test_192_khz_small_block_flac_stays_fed_at_start_phases_32_to_40_ms_with_the_screen_on(self):
        self.sweep_start_phases("flac192_512", False, PHASE_DELAYS_32_TO_40_MS)

    def test_176_khz_small_block_flac_stays_fed_at_start_phases_0_to_12_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac176_512", True, PHASE_DELAYS_0_TO_12_MS)

    def test_176_khz_small_block_flac_stays_fed_at_start_phases_16_to_28_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac176_512", True, PHASE_DELAYS_16_TO_28_MS)

    def test_176_khz_small_block_flac_stays_fed_at_start_phases_32_to_44_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac176_512", True, PHASE_DELAYS_32_TO_44_MS)

    def test_176_khz_small_block_flac_stays_fed_at_start_phases_48_to_64_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac176_512", True, PHASE_DELAYS_48_TO_64_MS)

    def test_176_khz_small_block_flac_stays_fed_at_start_phases_68_to_80_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac176_512", True, PHASE_DELAYS_68_TO_80_MS)

    def test_176_khz_small_block_flac_stays_fed_at_start_phases_84_to_96_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac176_512", True, PHASE_DELAYS_84_TO_96_MS)

    def test_176_khz_small_block_flac_stays_fed_at_start_phases_100_to_112_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac176_512", True, PHASE_DELAYS_100_TO_112_MS)

    def test_176_khz_small_block_flac_stays_fed_at_start_phases_116_to_128_ms_with_the_screen_off(self):
        self.sweep_start_phases("flac176_512", True, PHASE_DELAYS_116_TO_128_MS)

    def test_176_khz_small_block_flac_stays_fed_at_start_phases_0_to_12_ms_with_the_screen_on(self):
        self.sweep_start_phases("flac176_512", False, PHASE_DELAYS_0_TO_12_MS)

    def test_176_khz_small_block_flac_stays_fed_at_start_phases_16_to_28_ms_with_the_screen_on(self):
        self.sweep_start_phases("flac176_512", False, PHASE_DELAYS_16_TO_28_MS)

    def test_176_khz_small_block_flac_stays_fed_at_start_phases_32_to_40_ms_with_the_screen_on(self):
        self.sweep_start_phases("flac176_512", False, PHASE_DELAYS_32_TO_40_MS)

    def test_192_khz_flac_opus_vorbis_and_44_khz_flac_stay_fed_with_the_screen_off(self):
        for case in ("flac192_512", "opus48_20ms", "vorbis44", "flac44"):
            with self.subTest(case=case):
                rig, meter, frames = self.feed_rig(case)
                result = self.measure(rig, meter, frames, case, True)
                self.assert_fed(result)
                self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
