import json
import os
import random
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from echo_image_harness import EchoImageHarness, main, IDENTITY, fnv1a
from echo_emulator.fat_volume import Fat32Volume
from echo_emulator.playback import Rig
from echo_emulator.power import DecoderLoad, PowerMeter
from test_35_small_block_feed import encoded, FED_MS
from unicorn.arm_const import UC_ARM_REG_SP
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


def rgb565(pixel):
    red, green, blue = pixel
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def built_pack(rows):
    from PIL import Image
    manifest = json.loads((ROOT / "examples/themes/pocket-scope/theme.json").read_text())
    manifest["assets"] = [{"slot": "now_playing", "file": "now_playing.png",
                           "orientation": "landscape"}]
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        image = Image.new("RGB", (WIDTH, HEIGHT))
        image.putdata([pixel for row in rows for pixel in row])
        image.save(folder / "now_playing.png")
        (folder / "theme.json").write_text(json.dumps(manifest))
        return theme_pack.build(folder)


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
        meter = PowerMeter(rig.machine)
        frames = []
        rig.machine.hook_symbol("frame_pacer_done",
                                lambda: frames.append(rig.machine.milliseconds()))
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(SETTLE_MS)
        self.assertEqual(96000, rig.audio.rate)
        self.paint_stack(rig.machine)
        first_ms = rig.machine.milliseconds()
        result = meter.measure(rig, rig.session.card, MEASURE_MS)
        level = rig.audio.levels[-1][1] if rig.audio.levels else 0
        reads = [count for stamp, _, count in rig.session.card.read_log
                 if first_ms <= stamp < first_ms + MEASURE_MS]
        result.update(themed=pack is not None, stack_peak_bytes=self.stack_peak(rig.machine),
                      buffered_ms=round(max(0, level) * 1000 / rig.audio.rate),
                      card_kib_per_s=round(sum(reads) * 512 * 1000 / MEASURE_MS / 1024, 1),
                      frames_drawn=len([stamp for stamp in frames if stamp >= first_ms]))
        self.assert_healthy(rig.session)
        return result, list(rig.machine.panel.frame)

    def art_share(self, frame, art):
        return sum(1 for shown, wanted in zip(frame, art) if shown == wanted) / len(art)

    def report(self, results):
        keys = ("themed", "underruns", "output_underruns", "buffered_ms", "frames_drawn",
                "core0_busy_percent", "core0_passes_per_s", "card_kib_per_s", "stack_peak_bytes",
                "art_share")
        print(json.dumps([{key: result[key] for key in keys} for result in results]))
        destination = os.environ.get("ECHO_FEED_REPORT")
        if destination:
            with open(destination, "a", encoding="utf-8") as output:
                for result in results:
                    output.write(json.dumps(result) + "\n")

    def test_theme_background_with_a_scrolling_title_keeps_the_96_khz_feed(self):
        rows = photo_pixels()
        art = [rgb565(pixel) for row in rows for pixel in row]
        plain, plain_frame = self.measured(None)
        themed, themed_frame = self.measured(built_pack(rows))
        plain["art_share"] = round(self.art_share(plain_frame, art), 3)
        themed["art_share"] = round(self.art_share(themed_frame, art), 3)
        self.report([plain, themed])
        self.assertLessEqual(plain["art_share"], PLAIN_SHARE_MAX, plain)
        self.assertGreaterEqual(themed["art_share"], ART_SHARE_MIN, themed)
        for result in (plain, themed):
            self.assertEqual(0, result["underruns"], result)
            self.assertEqual(0, result["output_underruns"], result)
            self.assertGreaterEqual(result["buffered_ms"], FED_MS, result)
            self.assertGreaterEqual(result["frames_drawn"], MEASURE_MS * 3 // 1000, result)
            self.assertLessEqual(result["core0_busy_percent"], CORE0_LIMIT_PERCENT, result)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
