import json
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import uuid
import zlib
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import theme_pack


VALIDATOR = None
if "--validator" in sys.argv:
    at = sys.argv.index("--validator")
    VALIDATOR = sys.argv[at + 1]
    del sys.argv[at:at + 2]


class ThemeToolTests(unittest.TestCase):
    def folder(self, directory):
        folder = Path(directory)
        self.manifest = {
            "id": "dc53c369-e8a5-464d-bf04-94d10fc029fe", "name": "Original",
            "author": "Maker", "version": "1.2.3", "licence": "CC0-1.0",
            "homepage": "https://example.com/theme", "gallery": "https://example.com/gallery",
            "base": "graphite", "palette": {role: "#ffffff" for role in theme_pack.ROLES},
            "assets": [],
        }
        self.manifest["palette"]["background"] = "#000000"
        (folder / "theme.json").write_text(json.dumps(self.manifest))
        return folder

    def save(self, folder):
        (folder / "theme.json").write_text(json.dumps(self.manifest))

    def test_icon_slots_keep_their_numbers_and_boxes(self):
        names = (
            "music", "audiobook", "podcast", "now_playing", "mixtape", "audio", "settings",
            "cover_flow", "artist", "album_artist", "album", "genre", "folder", "song",
            "playlist", "favorite", "shuffle", "repeat", "display", "theme", "power", "sleep",
            "timeout", "controls", "start", "rescan", "diagnostics", "gain", "jack", "usb",
            "bluetooth", "key_plus", "key_minus", "key_play", "key_menu", "disc", "hub", "search",
            "computer", "play", "pause", "next", "previous", "seek", "queue", "speaker",
            "album_small", "shuffle_small", "bolt", "bluetooth_small", "lock", "song_small",
            "plug",
)
        for number, name in enumerate(names, start=1):
            self.assertEqual(256 + number, theme_pack.SLOTS["icon_" + name])
        self.assertEqual((20, 20), theme_pack.icon_box(theme_pack.SLOTS["icon_shuffle"]))
        self.assertEqual((14, 14), theme_pack.icon_box(theme_pack.SLOTS["icon_seek"]))
        self.assertEqual((9, 14), theme_pack.icon_box(theme_pack.SLOTS["icon_lock"]))

    def test_palette_and_all_metadata_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            data = theme_pack.build(folder)
            info = theme_pack.check(data)
            for field in ("name", "author", "version", "licence", "homepage", "gallery", "id"):
                self.assertEqual(self.manifest[field], info[field])
            self.assertEqual(512, len(data))

    def test_png_conversion_tiled_background_and_all_variants(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            image = Image.new("RGB", (3, 2), (255, 0, 0))
            image.save(folder / "list.png")
            self.manifest["assets"] = [{"slot": "list", "file": "list.png", "orientation": "any"}]
            self.save(folder)
            data = theme_pack.build(folder)
            asset = theme_pack.check(data)["assets"][0]
            self.assertEqual((1, 1, 1, 0), (asset["codec"], asset["entries"], asset["flags"], asset["alpha"]))
            Image.new("RGB", (20, 20)).save(folder / "music.png")
            self.manifest["assets"] = [{"slot": "icon_music", "file": "music.png", "text_size": "small"}]
            self.save(folder)
            self.assertEqual(257, theme_pack.check(theme_pack.build(folder))["assets"][0]["slot"])

    def test_each_image_chooses_its_codec_and_only_icons_keep_transparency(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            Image.merge("RGB", [Image.effect_noise((480, 222), sigma) for sigma in (20, 40, 60)]).save(
                folder / "home.png")
            icon = Image.new("RGBA", (20, 20), (255, 255, 255, 0))
            icon.putpixel((3, 3), (255, 0, 0, 128))
            icon.save(folder / "music.png")
            self.manifest["assets"] = [
                {"slot": "home", "file": "home.png", "orientation": "landscape", "codec": "lossless"},
                {"slot": "now_playing", "file": "home.png", "orientation": "landscape",
                 "codec": "indexed", "dither": "fs"},
                {"slot": "icon_music", "file": "music.png"}]
            self.save(folder)
            report = []
            info = theme_pack.check(theme_pack.build(folder, report=report))
            self.assertEqual([2, 1, 1], [asset["codec"] for asset in info["assets"]])
            self.assertEqual(99.0, report[0]["psnr_db"])
            self.assertLess(report[1]["psnr_db"], 99.0)
            self.assertGreater(info["assets"][2]["alpha"], 0)
            self.manifest["assets"] = [{"slot": "list", "file": "music.png", "orientation": "any"}]
            self.save(folder)
            with self.assertRaises(ValueError):
                theme_pack.build(folder)

    def test_now_playing_refuses_lossless_and_warns_about_indexed_battery_cost(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            Image.new("RGB", (480, 222), (40, 80, 120)).save(folder / "photo.png")
            self.manifest["assets"] = [{"slot": "now_playing", "file": "photo.png",
                                        "orientation": "landscape", "codec": "lossless"}]
            self.save(folder)
            with self.assertRaisesRegex(ValueError, "now_playing.*lossless.*indexed"):
                theme_pack.build(folder)
            self.manifest["assets"][0]["codec"] = "indexed"
            self.save(folder)
            data = theme_pack.build(folder)
            warnings = theme_pack.check(data)["warnings"]
            self.assertEqual(1, len([text for text in warnings
                                     if "now_playing" in text and "battery" in text]))
            hand_made = bytearray(theme_pack.build(self.home_copy(folder)))
            struct.pack_into("<H", hand_made, theme_pack.HEADER_BYTES, theme_pack.SLOTS["now_playing"])
            struct.pack_into("<I", hand_made, 12, zlib.crc32(hand_made[:12] + hand_made[16:]))
            with self.assertRaisesRegex(ValueError, "now_playing.*lossless.*indexed"):
                theme_pack.check(bytes(hand_made))
            pack = folder / "hand.gtheme"
            pack.write_bytes(hand_made)
            result = subprocess.run([sys.executable, str(Path(theme_pack.__file__)), "--check", str(pack)],
                                    capture_output=True, text=True)
            self.assertEqual(1, result.returncode)
            self.assertIn("now_playing", result.stderr)
            pack.write_bytes(data)
            result = subprocess.run([sys.executable, str(Path(theme_pack.__file__)), "--check", str(pack)],
                                    capture_output=True, text=True, check=True)
            self.assertTrue(any("battery" in text for text in json.loads(result.stdout)["warnings"]))

    def test_style_entries_and_the_cassette_label_warn_that_they_are_not_yet_applied(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            Image.new("RGB", (160, 48), (10, 20, 30)).save(folder / "label.png")
            self.manifest["style"] = [{"kind": "scene", "target": "sun_radius", "value": 20}]
            self.manifest["assets"] = [{"slot": "cassette_label", "file": "label.png"}]
            self.save(folder)
            warnings = theme_pack.check(theme_pack.build(folder))["warnings"]
            self.assertTrue(any("scene sun_radius" in text and "not yet applied" in text
                                for text in warnings), warnings)
            self.assertTrue(any("cassette_label" in text and "not yet drawn" in text
                                for text in warnings), warnings)
            self.manifest["style"] = []
            self.manifest["assets"] = []
            self.save(folder)
            warnings = theme_pack.check(theme_pack.build(folder))["warnings"]
            self.assertFalse(any("not yet" in text for text in warnings), warnings)

    def test_check_reads_at_most_one_byte_past_the_size_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            large = Path(directory) / "large.gtheme"
            with large.open("wb") as output:
                output.truncate(theme_pack.MAX_BYTES * 2)
            requests, returned = [], []
            real_read = theme_pack.os.read

            def counted(descriptor, size):
                requests.append(size)
                data = real_read(descriptor, size)
                returned.append(len(data))
                return data

            with mock.patch.object(theme_pack.os, "read", counted):
                self.assertEqual(theme_pack.MAX_BYTES + 1, len(theme_pack.read_capped(large)))
            self.assertEqual(theme_pack.MAX_BYTES + 1, sum(returned))
            self.assertLessEqual(max(requests), theme_pack.MAX_BYTES + 1)
            self.assertLessEqual(sum(requests), theme_pack.MAX_BYTES + 1)
            result = subprocess.run([sys.executable, str(Path(theme_pack.__file__)), "--check",
                                     str(large)], capture_output=True, text=True)
            self.assertEqual(1, result.returncode)
            self.assertIn("2 MiB", result.stderr)

    def home_copy(self, folder):
        self.manifest["assets"] = [{"slot": "home", "file": "photo.png",
                                    "orientation": "landscape", "codec": "lossless"}]
        self.save(folder)
        return folder

    def test_previews_show_each_asset_as_drawn(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            Image.new("RGB", (3, 2), (0, 0, 255)).save(folder / "list.png")
            self.manifest["assets"] = [{"slot": "list", "file": "list.png"}]
            self.save(folder)
            names = theme_pack.preview(theme_pack.build(folder), folder / "preview")
            with Image.open(folder / "preview" / names[0]) as image:
                self.assertEqual((3, 2), image.size)
                self.assertEqual((0, 0, 248), image.getpixel((1, 1)))

    def test_style_entries_and_version_1_packs_are_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            self.manifest["style"] = [{"kind": "scene", "target": "horizon", "value": 60},
                                      {"kind": "now_playing", "target": "progress_height", "value": 6}]
            self.save(folder)
            data = theme_pack.build(folder)
            self.assertEqual(8, struct.unpack_from("<H", data, 494)[0])
            for bad in ({"kind": "scene", "target": "horizon", "value": 90},
                        {"kind": "now_playing", "target": "progress_height", "value": 5}):
                self.manifest["style"] = [bad]
                self.save(folder)
                with self.assertRaises(ValueError):
                    theme_pack.build(folder)
            old = bytearray(data)
            old[4] = 1
            with self.assertRaisesRegex(ValueError, "version 1"):
                theme_pack.check(bytes(old))

    def test_rejects_crc_truncation_oversize_dimensions_and_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            good = theme_pack.build(folder)
            for data in (good[:-1], good + b"\0", good[:452] + b"!" + good[453:], b"\0" * (2097152 + 1)):
                with self.assertRaises(ValueError):
                    theme_pack.check(data)
            Image.new("RGB", (481, 222)).save(folder / "big.png")
            self.manifest["assets"] = [{"slot": "home", "file": "big.png", "orientation": "landscape"}]
            self.save(folder)
            with self.assertRaises(ValueError):
                theme_pack.build(folder)
            self.manifest["assets"][0]["file"] = "../else.png"
            self.save(folder)
            with self.assertRaises(ValueError):
                theme_pack.build(folder)

    def test_sign_verify_unknown_key_and_changed_signature(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            seed = folder / "private.key"
            seed.write_bytes(bytes(range(32)))
            data = theme_pack.build(folder, seed)
            public = theme_pack.public_key(seed)
            self.assertEqual("unknown-key", theme_pack.check(data)["signature"])
            self.assertEqual("verified", theme_pack.check(data, public)["signature"])
            damaged = data[:-1] + bytes([data[-1] ^ 1])
            with self.assertRaises(ValueError):
                theme_pack.check(damaged, public)
            self.assertEqual("unknown-key", theme_pack.check(damaged)["signature"])

    def test_c_validator_and_signer_agree_on_real_unsigned_and_signed_packs(self):
        if VALIDATOR is None:
            self.skipTest("C validator is supplied by CTest")
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            seed = folder / "seed.key"
            public = folder / "public.key"
            seed.write_bytes(bytes(range(32)))
            public.write_bytes(theme_pack.public_key(seed))
            Image.effect_noise((222, 480), 30).convert("RGB").save(folder / "home.png")
            Image.new("RGBA", (20, 20), (9, 9, 9, 60)).save(folder / "music.png")
            self.manifest["assets"] = [
                {"slot": "home", "file": "home.png", "orientation": "portrait", "codec": "lossless"},
                {"slot": "splash", "file": "home.png", "orientation": "portrait", "dither": "fs"},
                {"slot": "icon_music", "file": "music.png"}]
            self.save(folder)
            pack = folder / "sample.gtheme"
            for signing in (None, seed):
                pack.write_bytes(theme_pack.build(folder, signing))
                command = [VALIDATOR, "--check", str(pack)] + ([str(public)] if signing else [])
                subprocess.run(command, check=True, capture_output=True)
                damaged = bytearray(pack.read_bytes())
                damaged[-1] ^= 1
                pack.write_bytes(damaged)
                self.assertNotEqual(0, subprocess.run(command, capture_output=True).returncode)

    def test_low_contrast_warns_without_rejecting(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            self.manifest["palette"]["text"] = "#000000"
            self.save(folder)
            self.assertTrue(theme_pack.check(theme_pack.build(folder))["warnings"])

    def test_stable_id_is_preserved_and_metadata_is_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            first = theme_pack.check(theme_pack.build(folder))["id"]
            self.manifest["version"] = "2.0.0"
            self.save(folder)
            self.assertEqual(first, theme_pack.check(theme_pack.build(folder))["id"])
            for field, value in (("id", str(uuid.UUID(int=0))), ("name", "a" * 32),
                                 ("licence", "x" * 64), ("homepage", "file:///private"),
                                 ("version", "latest")):
                before = self.manifest[field]
                self.manifest[field] = value
                self.save(folder)
                with self.assertRaises(ValueError):
                    theme_pack.build(folder)
                self.manifest[field] = before


if __name__ == "__main__":
    unittest.main()
