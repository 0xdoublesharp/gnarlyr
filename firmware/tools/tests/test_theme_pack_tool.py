import json
import struct
import subprocess
import sys
import tempfile
import unittest
import uuid
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
            self.assertEqual(1, len(theme_pack.check(data)["assets"]))
            self.assertEqual(b"\x00\xf8" * 6, data[528:])
            Image.new("RGB", (20, 20)).save(folder / "music.png")
            self.manifest["assets"] = [{"slot": "icon_music", "file": "music.png", "text_size": "small"}]
            self.save(folder)
            self.assertEqual(257, theme_pack.check(theme_pack.build(folder))["assets"][0]["slot"])

    def test_rejects_crc_truncation_oversize_dimensions_and_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = self.folder(directory)
            good = theme_pack.build(folder)
            for data in (good[:-1], good + b"\0", good[:452] + b"!" + good[453:], b"\0" * (4194304 + 1)):
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
