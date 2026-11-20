import json
import os
import random
import struct
import sys
import tempfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from echo_image_harness import EchoImageHarness, main, IDENTITY, fnv1a, KEY_MENU, KEY_NEXT
from echo_emulator.fat_volume import Fat32Volume
from echo_emulator.playback import Rig, KEYS, CHORD_STEP_MS
from echo_emulator.power import DecoderLoad, PowerMeter
from test_35_small_block_feed import encoded, FED_MS
from unicorn.arm_const import UC_ARM_REG_SP
import art_codec
import theme_pack

ROOT = Path(__file__).resolve().parents[4]
THEMES = IDENTITY.data_directory + "/themes"
WIDTH, HEIGHT = 480, 222
SETTLE_MS = 1500
MEASURE_MS = 3000
STACK_FLOOR = 0x03000000
STACK_TOP = 0x03004000
STACK_PAINT = 0xA5C35A3C
STACK_MARGIN = 256
ART_SHARE_MIN = 0.25
PLAIN_SHARE_MAX = 0.01
CORE0_LIMIT_PERCENT = 60
CASE = "flac96_512"
TOAST_MS = 1500
TOAST_PHASES = 5
TOAST_PHASE_MS = 23
TOAST_GAP_MS = 2500
CLEARED_WITHIN_MS = 250
FAILED_RUN_MS = 1000
ROWS_FROM = 2048
DAC_RATE = 96000
DAC_MENU_RUN_MS = 1500
STACK_LIMIT_BYTES = 12 * 1024
LOWER_ROWS_FROM = 150


def photo_pixels(seed=7):
    rng = random.Random(seed)
    rows = []
    for y in range(HEIGHT):
        row = []
        for x in range(WIDTH):
            red = 40 + x * 160 // WIDTH + rng.randrange(-20, 21)
            green = 30 + y * 150 // HEIGHT + rng.randrange(-20, 21)
            blue = 90 + (x + y) * 100 // (WIDTH + HEIGHT) + rng.randrange(-20, 21)
            row.append(tuple(max(0, min(255, value)) for value in (red, green, blue)))
        rows.append(row)
    return rows


def built_pack(rows, assets=(("now_playing", "indexed"),)):
    from PIL import Image
    manifest = json.loads((ROOT / "examples/themes/pocket-scope/theme.json").read_text())
    manifest["assets"] = [{"slot": slot, "file": "now_playing.png", "orientation": "landscape",
                           "codec": codec, "dither": "none"} for slot, codec in assets]
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        image = Image.new("RGB", (WIDTH, HEIGHT))
        image.putdata([pixel for row in rows for pixel in row])
        image.save(folder / "now_playing.png")
        (folder / "theme.json").write_text(json.dumps(manifest))
        return theme_pack.build(folder)


def hand_made_lossless_now_playing(pack):
    data = bytearray(pack)
    struct.pack_into("<H", data, theme_pack.HEADER_BYTES + theme_pack.DESCRIPTOR_BYTES,
                     theme_pack.SLOTS["now_playing"])
    struct.pack_into("<I", data, 12, zlib.crc32(data[:12] + data[16:]))
    return bytes(data)


def drawn_art(pack):
    asset = theme_pack.check(pack)["assets"][0]
    payload = pack[asset["offset"]:asset["offset"] + asset["bytes"]]
    rows = art_codec.decode(payload, WIDTH, HEIGHT, [[0] * WIDTH for _ in range(HEIGHT)])
    return [value for row in rows for value in row]


def selection(identity):
    data = bytearray(1024)
    data[28:45] = identity + b"\0"
    struct.pack_into("<4sHHIIII", data, 0, b"DSTS", 1, 0, 1, 17, fnv1a(data[28:45]), 0)
    struct.pack_into("<I", data, 24, fnv1a(data[:24]))
    return bytes(data)


def feed_volume(pack):
    volume = Fat32Volume()
    volume.add_file(*encoded(CASE))
    if pack is not None:
        volume.add_directory(IDENTITY.data_directory)
        volume.add_directory("themes", IDENTITY.data_directory)
        volume.add_file("photo.gtheme", pack, directory=THEMES)
        volume.add_file("theme.bin", selection(pack[20:36]), directory=IDENTITY.data_directory)
    return volume


class EchoImageTest(EchoImageHarness):
    volume_for_session = None

    def session(self, *args, **kwargs):
        if self.volume_for_session is not None:
            kwargs["volume"] = self.volume_for_session
        return super().session(*args, **kwargs)

    def paint_stack(self, machine):
        low = machine.reg(UC_ARM_REG_SP) - STACK_MARGIN
        words = (low - STACK_FLOOR) // 4
        machine.uc.mem_write(STACK_FLOOR, struct.pack("<I", STACK_PAINT) * words)

    def stack_peak(self, machine):
        words = struct.unpack("<%dI" % ((STACK_TOP - STACK_FLOOR) // 4),
                              machine.read_memory(STACK_FLOOR, STACK_TOP - STACK_FLOOR))
        untouched = next((at for at, word in enumerate(words) if word != STACK_PAINT), len(words))
        return STACK_TOP - (STACK_FLOOR + untouched * 4)

    def measured(self, pack):
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=feed_volume(pack), module_elfs=self.module_elfs)
        DecoderLoad(rig.audio, self.elf.parent)
        home_frame = list(rig.machine.panel.frame)
        meter = PowerMeter(rig.machine)
        frames, draws = [], [0]
        per_frame = []

        def frame_done():
            frames.append(rig.machine.milliseconds())
            per_frame.append(draws[0])
            draws[0] = 0

        def art_drawn():
            draws[0] += 1

        rig.machine.hook_symbol("frame_pacer_done", frame_done)
        if pack is not None:
            rig.machine.hook_symbol("theme_art_module_draw", art_drawn)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        self.assertEqual(96000, rig.audio.rate)
        self.paint_stack(rig.machine)
        first_ms = rig.machine.milliseconds()
        first_frame = len(per_frame)
        result = meter.measure(rig, rig.session.card, MEASURE_MS)
        level = rig.audio.levels[-1][1] if rig.audio.levels else 0
        reads = [count for stamp, _, count in rig.session.card.read_log
                 if first_ms <= stamp < first_ms + MEASURE_MS]
        result.update(themed=pack is not None, stack_peak_bytes=self.stack_peak(rig.machine),
                      buffered_ms=round(max(0, level) * 1000 / rig.audio.rate),
                      card_kib_per_s=round(sum(reads) * 512 * 1000 / MEASURE_MS / 1024, 1),
                      frames_drawn=len([stamp for stamp in frames if stamp >= first_ms]),
                      art_draws_per_frame=per_frame[first_frame:],
                      full_frame_art_draws=max(per_frame[:first_frame], default=0))
        self.assert_healthy(rig.session)
        result["home_frame"] = home_frame
        return result, list(rig.machine.panel.frame)

    def art_share(self, frame, art):
        return sum(1 for shown, wanted in zip(frame, art) if shown == wanted) / len(art)

    def report(self, results):
        keys = ("themed", "codec", "underruns", "output_underruns", "buffered_ms", "frames_drawn",
                "core0_busy_percent", "core0_passes_per_s", "card_kib_per_s", "stack_peak_bytes",
                "art_share")
        print(json.dumps([{key: result.get(key) for key in keys} for result in results]))
        destination = os.environ.get("ECHO_FEED_REPORT")
        if destination:
            with open(destination, "a", encoding="utf-8") as output:
                for result in results:
                    kept = {key: value for key, value in result.items() if key != "home_frame"}
                    output.write(json.dumps(kept) + "\n")

    def compared(self, codec):
        pack = built_pack(photo_pixels())
        art = drawn_art(pack)
        plain, plain_frame = self.measured(None)
        themed, themed_frame = self.measured(pack)
        plain["art_share"] = round(self.art_share(plain_frame, art), 3)
        themed["art_share"] = round(self.art_share(themed_frame, art), 3)
        themed["codec"] = codec
        self.report([plain, themed])
        self.assertLessEqual(plain["art_share"], PLAIN_SHARE_MAX, plain)
        self.assertGreaterEqual(themed["art_share"], ART_SHARE_MIN, themed)
        self.assert_fed(plain, themed)
        return plain, themed

    def assert_fed(self, *results):
        for result in results:
            self.assertEqual(0, result["underruns"], result)
            self.assertEqual(0, result["output_underruns"], result)
            self.assertGreaterEqual(result["buffered_ms"], FED_MS, result)
            self.assertGreaterEqual(result["frames_drawn"], MEASURE_MS * 3 // 1000, result)

    def test_theme_background_with_a_scrolling_title_keeps_the_96_khz_feed(self):
        plain, themed = self.compared("indexed")
        for result in (plain, themed):
            self.assertLessEqual(result["core0_busy_percent"], CORE0_LIMIT_PERCENT, result)

    def test_marquee_steps_and_new_seconds_redraw_only_their_bands(self):
        plain, themed = self.compared("indexed")
        draws = themed["art_draws_per_frame"]
        full = themed["full_frame_art_draws"]
        print(json.dumps({"full_frame_art_draws": full, "art_draws_per_frame": draws}))
        self.assertGreaterEqual(full, 8, draws)
        self.assertLessEqual(max(draws), full * 2 // 3, draws)
        self.assertGreaterEqual(len([count for count in draws if 0 < count <= full // 4]),
                                len(draws) * 3 // 4, draws)

    def test_a_lossless_now_playing_image_draws_theme_colours_and_the_rest_of_the_pack_applies(self):
        built = built_pack(photo_pixels(), (("home", "lossless"), ("splash", "lossless")))
        art = drawn_art(built)
        pack = hand_made_lossless_now_playing(built)
        with self.assertRaisesRegex(ValueError, "now_playing"):
            theme_pack.check(pack)
        themed, frame = self.measured(pack)
        themed["art_share"] = round(self.art_share(frame, art), 3)
        themed["codec"] = "lossless"
        home_share = self.art_share(themed["home_frame"], art)
        self.report([themed])
        self.assertLessEqual(themed["art_share"], PLAIN_SHARE_MAX, themed)
        self.assertGreaterEqual(home_share, ART_SHARE_MIN, home_share)
        self.assert_fed(themed)
        self.assertLessEqual(themed["core0_busy_percent"], CORE0_LIMIT_PERCENT, themed)

    def test_a_toast_ending_during_a_scrolling_title_redraws_the_whole_screen(self):
        from unicorn.arm_const import UC_ARM_REG_R0
        pack = built_pack(photo_pixels())
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=feed_volume(pack), module_elfs=self.module_elfs)
        machine = rig.machine
        frames, draws, shown = [], [0], []
        def frame_done():
            frames.append((machine.milliseconds(), draws[0]))
            draws[0] = 0
        def art_drawn():
            draws[0] += 1
        machine.hook_symbol("frame_pacer_done", frame_done)
        machine.hook_symbol("theme_art_module_draw", art_drawn)
        machine.hook_symbol("toast_show", lambda: shown.append(machine.milliseconds()))
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        full = max(count for _, count in frames)
        chord = [(KEYS["play"], CHORD_STEP_MS), (KEYS["next"], CHORD_STEP_MS),
                 (KEYS["play"], CHORD_STEP_MS)]
        for phase in range(TOAST_PHASES):
            machine.press_ladder(chord)
            rig.run_for(TOAST_GAP_MS + phase * TOAST_PHASE_MS)
        self.assertGreaterEqual(len(shown), TOAST_PHASES, shown)
        for stamp in shown:
            ended = stamp + TOAST_MS
            whole = [count for at, count in frames
                     if ended <= at <= ended + CLEARED_WITHIN_MS and count >= full // 2]
            self.assertTrue(whole, (stamp, full, [(at, count) for at, count in frames
                                                  if ended - 200 <= at <= ended + 400]))
        self.assert_healthy(rig.session)


    def test_a_card_read_that_fails_during_a_band_redraw_falls_back_to_theme_colours(self):
        pack = built_pack(photo_pixels())
        art = drawn_art(pack)
        asset = theme_pack.check(pack)["assets"][0]
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=feed_volume(pack), module_elfs=self.module_elfs)
        machine, session = rig.machine, rig.session
        frames, draws = [], [0]
        def frame_done():
            frames.append((machine.milliseconds(), draws[0]))
            draws[0] = 0
        def art_drawn():
            draws[0] += 1
        machine.hook_symbol("frame_pacer_done", frame_done)
        machine.hook_symbol("theme_art_module_draw", art_drawn)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        self.assertGreaterEqual(self.art_share(machine.panel.frame, art), ART_SHARE_MIN)
        sectors = list(session.file_sectors(THEMES + "/photo.gtheme"))
        first = asset["offset"] + ROWS_FROM
        for at in range(first // 512 + 1, (asset["offset"] + asset["bytes"]) // 512):
            session.disk.write(sectors[at], bytes(512))
        failed_at = machine.milliseconds()
        rig.run_for(FAILED_RUN_MS)
        after = [(at, count) for at, count in frames if at >= failed_at]
        share = self.art_share(machine.panel.frame, art)
        self.assertLessEqual(share, PLAIN_SHARE_MAX, (share, after))
        self.assertTrue(any(count == 0 for _, count in after), after)
        self.assertEqual([], rig.audio.underruns(failed_at))
        self.assert_healthy(session)


    def test_a_streaming_usb_dac_menu_over_a_lossless_list_background_keeps_its_stack(self):
        import test_28_usb_audio
        pack = built_pack(photo_pixels(), (("list", "lossless"), ("home", "indexed")))
        self.volume_for_session = feed_volume(pack)
        session, audio, _ = test_28_usb_audio.EchoImageTest.dac_session(self, DAC_RATE)
        machine = session.machine
        self.paint_stack(machine)
        draws = machine.trace("theme_art_module_draw")
        for key in (KEY_MENU, KEY_NEXT, KEY_NEXT):
            self.press(machine, key)
        machine.run(until_ms=machine.milliseconds() + DAC_MENU_RUN_MS)
        peak = self.stack_peak(machine)
        print(json.dumps({"usb_dac_menu_stack_peak_bytes": peak, "art_draws": len(draws)}))
        self.assertTrue(draws)
        self.assertLessEqual(peak, STACK_LIMIT_BYTES, peak)
        self.assertEqual(0, audio.output_underruns())
        self.assert_healthy(session)

    def test_a_card_read_that_fails_in_the_usb_dac_menu_repaints_it_in_theme_colours(self):
        import test_28_usb_audio
        pack = built_pack(photo_pixels(), (("list", "lossless"), ("home", "indexed")))
        art = drawn_art(pack)
        asset = theme_pack.check(pack)["assets"][0]
        self.volume_for_session = feed_volume(pack)
        session, audio, _ = test_28_usb_audio.EchoImageTest.dac_session(self, DAC_RATE)
        machine = session.machine
        for key in (KEY_MENU, KEY_NEXT):
            self.press(machine, key)
        machine.run(until_ms=machine.milliseconds() + DAC_MENU_RUN_MS)
        before = self.art_share(machine.panel.frame, art)
        sectors = list(session.file_sectors(THEMES + "/photo.gtheme"))
        table = asset["offset"] + theme_pack.table_start(asset)
        row = struct.unpack_from("<I", pack, table + 4 * LOWER_ROWS_FROM)[0]
        first = asset["offset"] + row
        for at in range(first // 512 + 1, (asset["offset"] + asset["bytes"]) // 512):
            session.disk.write(sectors[at], bytes(512))
        self.press(machine, KEY_NEXT)
        machine.run(until_ms=machine.milliseconds() + DAC_MENU_RUN_MS)
        after = self.art_share(machine.panel.frame, art)
        print(json.dumps({"usb_dac_menu_art_share_before": round(before, 3),
                          "after": round(after, 3)}))
        self.assertGreaterEqual(before, ART_SHARE_MIN / 2, before)
        self.assertLessEqual(after, PLAIN_SHARE_MAX, after)
        self.assertEqual(0, audio.output_underruns())
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
