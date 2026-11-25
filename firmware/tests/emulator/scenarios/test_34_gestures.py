import os
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from echo_image_harness import (
    EchoImageHarness,
    PROJECT,
    main,
    struct,
)

PATTERN = (0, 8485, 12000, 8485, 0, -8485, -12000, -8485)
TRACKS = (("01 First.wav", 44100, 30), ("02 Second.wav", 48000, 20), ("03 Third.wav", 44100, 25),
          ("04 Fourth.wav", 48000, 20), ("05 Fifth.wav", 44100, 30), ("06 Sixth.wav", 48000, 25))
LONG_TRACKS = (("01 First.wav", 44100, 200), ("02 Second.wav", 44100, 200),
               ("03 Third.wav", 44100, 200))
REFUSED_TRACKS = (("01 First.wav", 44100, 30), ("02 Refused.wav", 7000, 30),
                  ("03 Third.wav", 44100, 30))
PLAY = 250
MENU = 350
VOLUME_KEYS = (35, 100)
TAP_MS = 150
LONG_HOLD_MS = 1300
ENGINE_STATE_OFFSET = 44
PLAYER_STATE_OFFSET = 48 * 16 + 8
PLAYER_PLAYING = 1
PLAYER_PAUSED = 2
EVENT_CAPACITY = 48
EVENT_BYTES = 16
EVENT_NAMES = ("key", "screen", "player", "underrun", "output_underrun", "refusal", "storage",
               "usb_start", "usb_end", "script", "power", "usb_connect", "scan", "cover",
               "display_fallback", "ladder")
ENGINE_STOPPED = 0
SETTLE_MS = 600
CHECKS = 4
CHECK_GAP_MS = 250
MP3 = PROJECT / "tests" / "fixtures" / "mp3" / "cbr_192_48k_stereo_bare.mp3"
MP3_REPEATS = 24
LISTEN_MS = 2500
END_HOLD_MS = 600 + 4000
END_PLAY_MS = 1500
LAST_SECOND_LISTEN_MS = 800
PREVIEW_HOLD_MS = 600 + 1500
RESUMED_LISTEN_MS = 1200
DARK_LISTEN_MS = 4000
CORE1_INSTRUCTIONS_PER_US = 100
OGG_LISTEN_MS = 1200
PREFILL_WAIT_MS = 1500
GESTURES = int(os.environ.get("ECHO_GESTURES", "36"))
SEEDS = tuple(int(seed) for seed in os.environ.get("ECHO_GESTURE_SEEDS", "1").split(","))


def wav_file(rate, tenths):
    frames = rate * tenths // 10
    samples = []
    for frame in range(frames):
        value = PATTERN[frame % len(PATTERN)]
        samples.extend((value, -value))
    pcm = struct.pack("<%dh" % len(samples), *samples)
    return struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ", 16, 1, 2,
                       rate, rate * 4, 4, 16, b"data", len(pcm)) + pcm


def small_block_flac(rate_code, rate, block_code, block, seconds):
    from echo_emulator.flac_image import crc8, crc16, utf8_number
    samples = bytearray()
    for index in range(block):
        samples += struct.pack(">h", PATTERN[index % len(PATTERN)])
    body = (bytes([0x02]) + bytes(samples)) * 2
    frames = bytearray()
    count = rate * seconds // block
    sizes = set()
    for number in range(count):
        header = bytes([0xFF, 0xF8, block_code << 4 | rate_code, 0x10]) + utf8_number(number)
        header += bytes([crc8(header)])
        frame = header + body
        frame += struct.pack(">H", crc16(frame))
        sizes.add(len(frame))
        frames += frame
    packed = rate << 44 | 1 << 41 | 15 << 36 | count * block
    info = (struct.pack(">HH", block, block) + min(sizes).to_bytes(3, "big")
            + max(sizes).to_bytes(3, "big") + packed.to_bytes(8, "big") + bytes(16))
    return b"fLaC" + bytes([0x80, 0, 0, 34]) + info + bytes(frames)


class Layers:
    def __init__(self, rig):
        self.rig = rig
        machine = rig.machine
        self.symbols = machine.symbols
        self.machine = machine

    def byte(self, name):
        return self.machine.read_memory(self.symbols[name], 1)[0]

    def word(self, address):
        return struct.unpack("<I", self.machine.read_memory(address, 4))[0]

    def snapshot(self):
        audio = self.rig.audio
        return {"app": self.word(self.symbols["loop"] + PLAYER_STATE_OFFSET) & 0xFF,
                "engine": self.machine.read_memory(self.symbols["engine"] + ENGINE_STATE_OFFSET, 1)[0],
                "dma": audio.running and audio.suspend_depth == 0,
                "path_up": self.byte("path_up"),
                "dac_up": self.byte("dac_up"),
                "muted": self.byte("muted"),
                "applied": self.byte("chip_volume"),
                "wanted": self.byte("volume_value"),
                "transferring": self.byte("transferring")}

    def events(self, count=24):
        base = self.symbols["loop"]
        recorded = self.word(base + EVENT_CAPACITY * EVENT_BYTES)
        found = []
        for index in range(max(0, recorded - count), recorded):
            raw = self.machine.read_memory(base + index % EVENT_CAPACITY * EVENT_BYTES, EVENT_BYTES)
            stamp, value, kind = struct.unpack_from("<IIB", raw)
            text = raw[9:16].split(b"\0")[0].decode("ascii", "replace")
            found.append("%d %s %d %s" % (stamp, EVENT_NAMES[kind] if kind < len(EVENT_NAMES)
                                          else kind, value, text))
        return "\n".join(found)

    @staticmethod
    def audible(state):
        return (state["dma"] and state["dac_up"] and not state["muted"]
                and state["engine"] != ENGINE_STOPPED)

    @classmethod
    def problem(cls, state):
        if (state["app"] == PLAYER_PLAYING) != cls.audible(state):
            return "player and output disagree"
        if cls.audible(state) and state["applied"] != state["wanted"]:
            return "volume not applied"
        return None


class Gestures:
    def __init__(self, rig, seed):
        self.rig = rig
        self.machine = rig.machine
        self.random = random.Random(seed)

    def ladder(self, steps):
        released = self.machine.press_ladder(steps)
        self.machine.run(until_ms=released + 60)
        return released

    def volume(self):
        return self.random.choice(VOLUME_KEYS)

    def act(self):
        pick = self.random.randrange(14)
        volume = self.volume()
        held = 300 + self.random.randrange(1500)
        if pick in (0, 1):
            return "play tap", self.ladder([(PLAY, TAP_MS)])
        if pick == 2:
            return "play hold", self.ladder([(PLAY, 700 + self.random.randrange(600))])
        if pick == 3:
            return "volume tap", self.ladder([(volume, TAP_MS)])
        if pick == 4:
            return "volume hold", self.ladder([(volume, held)])
        if pick == 5:
            return "chord tap", self.ladder([(PLAY, TAP_MS), (volume, TAP_MS), (PLAY, TAP_MS)])
        if pick == 6:
            return "chord hold", self.ladder([(PLAY, TAP_MS), (volume, held), (PLAY, TAP_MS)])
        if pick == 7:
            return "split chord", self.ladder([(PLAY, TAP_MS), (volume, held)])
        if pick == 8:
            return "long play then volume", self.ladder(
                [(PLAY, 700 + self.random.randrange(600)), (volume, TAP_MS), (PLAY, TAP_MS)])
        if pick == 11:
            return "menu chord tap", self.ladder([(MENU, TAP_MS), (volume, TAP_MS), (MENU, TAP_MS)])
        if pick == 12:
            return "menu chord hold", self.ladder([(MENU, TAP_MS), (volume, held), (MENU, TAP_MS)])
        if pick == 13:
            return "long menu then volume", self.ladder(
                [(MENU, 700 + self.random.randrange(600)), (volume, TAP_MS), (MENU, TAP_MS)])
        if pick == 9:
            start = self.machine.milliseconds() + 20
            self.machine.hold_power(start, TAP_MS)
            self.machine.run(until_ms=start + TAP_MS + 60)
            return "power tap", start
        wait = 200 + self.random.randrange(2500)
        self.machine.run(until_ms=self.machine.milliseconds() + wait)
        return "wait %d" % wait, self.machine.milliseconds()


class EchoImageTest(EchoImageHarness):
    def gesture_rig(self, tracks=TRACKS):
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        volume = Fat32Volume()
        for name, rate, tenths in tracks:
            volume.add_file(name, wav_file(rate, tenths))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        return rig

    def lasting_problem(self, rig, layers):
        found = []
        for _ in range(CHECKS):
            rig.run_for(CHECK_GAP_MS)
            state = layers.snapshot()
            found.append((Layers.problem(state), state))
        return found[-1] if all(problem for problem, _ in found) else (None, found[-1][1])

    def test_random_play_and_volume_gestures_keep_the_player_and_the_output_together(self):
        for seed in SEEDS:
            rig = self.gesture_rig()
            rig.open_songs()
            rig.play_song(1)
            rig.run_for(SETTLE_MS)
            layers = Layers(rig)
            gestures = Gestures(rig, seed)
            history = []
            for index in range(GESTURES):
                name, stamp = gestures.act()
                rig.run_for(SETTLE_MS)
                problem, state = self.lasting_problem(rig, layers)
                history.append("%d %s at %d: %s" % (index, name, stamp, state))
                if problem:
                    print("\n[gestures] seed %d: %s\n%s" % (seed, problem, "\n".join(history[-8:])))
                self.assertIsNone(problem, "seed %d gesture %d %s" % (seed, index, name))
            self.assert_healthy(rig.session)

    def test_long_play_or_menu_holds_before_a_volume_tap_skip_or_scrub_and_leave_the_volume(self):
        rig = self.gesture_rig(LONG_TRACKS)
        layers = Layers(rig)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        machine = rig.machine
        seeks = []
        machine.hook_symbol("hal_audio_seek", lambda: seeks.append(machine.milliseconds()))

        def track():
            return layers.word(layers.symbols["loop"] + PLAYER_STATE_OFFSET) >> 8

        def screens():
            return layers.events(EVENT_CAPACITY).count(" screen ")

        volume = layers.byte("volume_value")
        first = track()
        gestures = Gestures(rig, 0)
        gestures.ladder([(PLAY, LONG_HOLD_MS), (VOLUME_KEYS[1], TAP_MS), (PLAY, TAP_MS)])
        rig.run_for(SETTLE_MS)
        self.assertNotEqual(first, track(), layers.events())
        self.assertEqual(volume, layers.byte("volume_value"), layers.events())
        skipped = track()
        shown = screens()
        gestures.ladder([(MENU, LONG_HOLD_MS), (VOLUME_KEYS[1], TAP_MS), (MENU, TAP_MS)])
        rig.run_for(SETTLE_MS)
        self.assertEqual(1, len(seeks), layers.events())
        self.assertEqual(skipped, track(), layers.events())
        self.assertEqual(volume, layers.byte("volume_value"), layers.events())
        self.assertEqual(shown, screens(), layers.events(EVENT_CAPACITY))
        self.assertEqual(PLAYER_PLAYING, layers.snapshot()["app"])
        gestures.ladder([(PLAY, LONG_HOLD_MS)])
        self.assertEqual(shown + 1, screens(), layers.events(EVENT_CAPACITY))
        self.assertEqual(volume, layers.byte("volume_value"), layers.events())
        rig.press("back")
        gestures.ladder([(VOLUME_KEYS[1], TAP_MS)])
        rig.run_for(SETTLE_MS)
        self.assertNotEqual(volume, layers.byte("volume_value"), layers.events())
        self.assertEqual(1, len(seeks), layers.events())
        self.assertEqual(skipped, track(), layers.events())
        self.assertEqual(PLAYER_PLAYING, layers.snapshot()["app"])
        self.assert_healthy(rig.session)

    def track_of(self, layers):
        return layers.word(layers.symbols["loop"] + PLAYER_STATE_OFFSET) >> 8

    def scrub_to_the_end(self, rig, layers):
        machine = rig.machine
        seeks = []
        machine.hook_symbol("hal_audio_seek", lambda: seeks.append(machine.milliseconds()))
        released = machine.press_ladder([(MENU, TAP_MS), (VOLUME_KEYS[1], END_HOLD_MS),
                                         (MENU, TAP_MS)])
        machine.run(until_ms=released - TAP_MS)
        self.assertEqual([], seeks, layers.events())
        machine.run(until_ms=released + SETTLE_MS)
        self.assertEqual(1, len(seeks), layers.events())
        return released

    def test_a_paused_scrub_to_the_end_waits_silently_then_plays_into_a_track_at_another_rate(self):
        rig = self.gesture_rig()
        layers = Layers(rig)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        rig.press("play")
        paused = layers.snapshot()
        self.assertNotEqual(PLAYER_PLAYING, paused["app"], paused)
        released = self.scrub_to_the_end(rig, layers)
        rig.machine.run(until_ms=released + PREFILL_WAIT_MS)
        problem, state = self.lasting_problem(rig, layers)
        self.assertIsNone(problem, "%s\n%s" % (state, layers.events()))
        self.assertEqual(PLAYER_PAUSED, state["app"], "%s\n%s" % (state, layers.events()))
        self.assertEqual(0, self.track_of(layers), layers.events())
        self.assertFalse(Layers.audible(state), state)
        resumed = rig.press("play")
        rig.machine.run(until_ms=resumed + LAST_SECOND_LISTEN_MS)
        self.assertEqual(0, self.track_of(layers), layers.events())
        self.assertIsNone(Layers.problem(layers.snapshot()), layers.events())
        self.assertEqual([], rig.audio.underruns(resumed))
        rig.run_for(END_PLAY_MS)
        self.assertEqual(1, self.track_of(layers), layers.events())
        self.assertEqual(48000, rig.audio.rate)
        self.assert_playing_cleanly(rig, layers, rig.machine.milliseconds(), RESUMED_LISTEN_MS)
        self.assert_healthy(rig.session)

    def test_a_paused_scrub_to_the_end_plays_on_past_an_unreadable_track(self):
        rig = self.gesture_rig(REFUSED_TRACKS)
        layers = Layers(rig)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        rig.press("play")
        released = self.scrub_to_the_end(rig, layers)
        rig.machine.run(until_ms=released + PREFILL_WAIT_MS)
        problem, state = self.lasting_problem(rig, layers)
        self.assertIsNone(problem, "%s\n%s" % (state, layers.events()))
        self.assertEqual(PLAYER_PAUSED, state["app"], "%s\n%s" % (state, layers.events()))
        self.assertEqual(0, self.track_of(layers), layers.events())
        self.assertFalse(Layers.audible(state), state)
        rig.press("play")
        rig.run_for(END_PLAY_MS)
        self.assertEqual(2, self.track_of(layers), layers.events())
        self.assert_playing_cleanly(rig, layers, rig.machine.milliseconds(), RESUMED_LISTEN_MS)
        self.assert_healthy(rig.session)

    def test_a_menu_hold_previews_without_seeking_and_seeks_once_on_release(self):
        rig = self.gesture_rig(LONG_TRACKS)
        layers = Layers(rig)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        machine = rig.machine
        seeks = []
        machine.hook_symbol("hal_audio_seek", lambda: seeks.append(machine.milliseconds()))
        volume = layers.byte("volume_value")
        released = machine.press_ladder([(MENU, TAP_MS), (VOLUME_KEYS[1], PREVIEW_HOLD_MS),
                                         (MENU, TAP_MS)])
        machine.run(until_ms=released - TAP_MS)
        self.assertEqual([], seeks, layers.events())
        self.assertEqual(PLAYER_PLAYING, layers.snapshot()["app"])
        gliding = released - TAP_MS - PREVIEW_HOLD_MS + 600
        starved = [stamp for stamp, level in rig.audio.levels
                   if gliding <= stamp < released - TAP_MS and level < 0]
        self.assertEqual([], starved[:5])
        machine.run(until_ms=released + SETTLE_MS)
        self.assertEqual(1, len(seeks), layers.events())
        self.assertEqual(0, self.track_of(layers), layers.events())
        self.assertEqual(volume, layers.byte("volume_value"), layers.events())
        self.assert_playing_cleanly(rig, layers, released, RESUMED_LISTEN_MS)
        self.assert_healthy(rig.session)

    def test_a_bluetooth_restart_whose_transfer_fails_stops_the_track(self):
        rig = self.gesture_rig()
        layers = Layers(rig)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        self.assertEqual(PLAYER_PLAYING, layers.snapshot()["app"])
        dmac = rig.audio.real_dmac
        starts = dmac.starts

        def occupy_channel():
            dmac.audio_channel_busy = True

        rig.machine.hook_symbol("dmac_start_loop", occupy_channel)
        self.assertEqual(0, self.call_firmware(rig.machine, "board_audio_bluetooth_start"))
        self.assertTrue(dmac.audio_channel_busy)
        dmac.audio_channel_busy = False
        self.assertEqual(starts, dmac.starts)
        rig.run_for(SETTLE_MS)
        state = layers.snapshot()
        self.assertEqual(ENGINE_STOPPED, state["engine"], state)
        self.assertNotEqual(PLAYER_PLAYING, state["app"], "%s\n%s" % (state, layers.events()))
        self.assertIsNone(Layers.problem(state), state)
        self.assert_healthy(rig.session)

    def test_screen_off_playback_keeps_a_96_khz_flac_with_512_sample_blocks_fed(self):
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        volume = Fat32Volume()
        volume.add_file("Small blocks.flac", small_block_flac(0xB, 96000, 0x9, 512, 12))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        rig.audio.core1.instructions_per_us = CORE1_INSTRUCTIONS_PER_US
        layers = Layers(rig)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        self.assertEqual(96000, rig.audio.rate)
        start = rig.machine.milliseconds() + 20
        rig.machine.hold_power(start, TAP_MS)
        rig.machine.run(until_ms=start + TAP_MS + SETTLE_MS)
        dark = rig.machine.milliseconds()
        self.assertFalse(rig.machine.read_memory(rig.machine.symbols["display_lit"], 1)[0]
                         if "display_lit" in rig.machine.symbols else False)
        rig.run_for(DARK_LISTEN_MS)
        state = layers.snapshot()
        self.assertEqual(PLAYER_PLAYING, state["app"], "%s\n%s" % (state, layers.events()))
        starved = [stamp for stamp, level in rig.audio.levels if stamp >= dark and level < 0]
        self.assertEqual([], starved[:5], "%d starved samples" % len(starved))
        self.assert_healthy(rig.session)

    def dark_starvation(self, rig, since, until):
        return [stamp for stamp, level in rig.audio.levels if since <= stamp < until and level < 0]

    def test_screen_off_playback_keeps_vorbis_and_opus_fed(self):
        from echo_emulator.core1_bridge import CORE1_FLAC, CORE1_OPUS, CORE1_VORBIS, Core1Bridge
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        fixtures = PROJECT / "tests" / "fixtures"
        volume = Fat32Volume()
        volume.add_file("01 Vorbis.ogg", (fixtures / "vorbis" / "cropped44.ogg").read_bytes())
        volume.add_file("02 Opus.opus", (fixtures / "opus" / "stereo48.opus").read_bytes())
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        programs = {program: self.elf.with_name("core1_%s.bin" % name).read_bytes()
                    for program, name in ((CORE1_VORBIS, "vorbis"), (CORE1_OPUS, "opus"))}
        programs[CORE1_FLAC] = rig.audio.core1.programs[CORE1_FLAC]
        Core1Bridge(rig.audio, programs, CORE1_INSTRUCTIONS_PER_US)
        layers = Layers(rig)
        rig.open_songs()
        rig.play_song(1)
        start = rig.machine.milliseconds() + 20
        rig.machine.hold_power(start, TAP_MS)
        rig.machine.run(until_ms=start + TAP_MS + SETTLE_MS)
        dark = rig.machine.milliseconds()
        rig.run_for(OGG_LISTEN_MS)
        self.assertEqual(PLAYER_PLAYING, layers.snapshot()["app"], layers.events())
        self.assertEqual([], self.dark_starvation(rig, dark, rig.machine.milliseconds())[:5])
        released = rig.machine.press_ladder([(PLAY, TAP_MS), (VOLUME_KEYS[1], TAP_MS),
                                             (PLAY, TAP_MS)])
        rig.machine.run(until_ms=released + SETTLE_MS)
        skipped = rig.machine.milliseconds()
        self.assertEqual(1, layers.word(layers.symbols["loop"] + PLAYER_STATE_OFFSET) >> 8,
                         layers.events())
        rig.run_for(OGG_LISTEN_MS)
        self.assertEqual(PLAYER_PLAYING, layers.snapshot()["app"], layers.events())
        self.assertEqual([], self.dark_starvation(rig, skipped, rig.machine.milliseconds())[:5])
        self.assert_healthy(rig.session)

    def remount_rig(self):
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        song = MP3.read_bytes() * MP3_REPEATS
        slot = self.with_card(files=[("A.mp3", song, None), ("B.mp3", song, None)])
        volume = Fat32Volume()
        volume.add_file("INSIDE.WAV", wav_file(44100, 200))
        volume.add_file("INSIDE.MP3", song)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs, prepare=slot.insert)
        return rig, slot

    def assert_playing_cleanly(self, rig, layers, since, listen=LISTEN_MS):
        rig.run_for(listen)
        state = layers.snapshot()
        self.assertEqual(PLAYER_PLAYING, state["app"], "%s\n%s" % (state, layers.events()))
        self.assertIsNone(Layers.problem(state), state)
        starved = [stamp for stamp, level in rig.audio.levels
                   if since + listen // 2 <= stamp < since + listen and level < 0]
        self.assertEqual([], starved[:5])

    def test_playback_resumes_and_paused_skips_wait_after_a_session_that_changed_internal_music(self):
        from test_33_combined_library import host_rewrites_internal_entry
        rig, slot = self.remount_rig()
        layers = Layers(rig)
        rig.open_songs()
        self.assert_playing_cleanly(rig, layers, rig.play_song(1))
        rig.press("play")
        self.assertIsNone(Layers.problem(layers.snapshot()))
        host_rewrites_internal_entry(self, rig.session, b"INSIDE  WAV", changed=True)
        rig.settle(1000)
        self.assertIsNone(Layers.problem(layers.snapshot()), layers.snapshot())
        rig.press("play")
        self.assert_playing_cleanly(rig, layers, rig.press("play"))
        for name in ("next", "next", "previous", "next"):
            rig.press("play")
            paused = layers.snapshot()
            self.assertIsNone(Layers.problem(paused), paused)
            rig.skip(name)
            rig.run_for(SETTLE_MS)
            state = layers.snapshot()
            self.assertIsNone(Layers.problem(state), (name, state))
            self.assertEqual(PLAYER_PAUSED, state["app"], (name, state, layers.events()))
            self.assertFalse(Layers.audible(state), (name, state))
            self.assert_playing_cleanly(rig, layers, rig.press("play"))
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
