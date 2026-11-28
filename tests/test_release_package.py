import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import firmware
import release_package as release
from echo_language import pack_tools

VERSION = release.IDENTITY["version"]
COMMIT = "c" * 40
STOCK = release.STOCK_IMAGE


def image(build):
    data = bytearray(0x500004)
    data[release.BUILD_FIELD] = build.encode().ljust(release.FIELD, b"\0")
    return bytes(data)


class LabelTests(unittest.TestCase):
    def test_a_release_tag_has_no_label(self):
        self.assertEqual("", release.label("v" + VERSION))

    def test_a_pre_release_tag_gives_its_label(self):
        self.assertEqual("beta.1", release.label(f"v{VERSION}-beta.1"))
        self.assertEqual(VERSION + "-beta.1", release.release_name("beta.1"))

    def test_a_tag_for_another_version_is_refused(self):
        with self.assertRaisesRegex(ValueError, "identity.json"):
            release.label("v0.0.1-beta.1")

    def test_a_label_longer_than_the_build_field_is_refused(self):
        with self.assertRaisesRegex(ValueError, "8 characters"):
            release.label(f"v{VERSION}-beta.1234")

    def test_malformed_tags_are_refused(self):
        for tag in (VERSION, f"v{VERSION}-", f"v{VERSION}-beta 1", f"v{VERSION}-.b", f"v{VERSION}\n"):
            with self.assertRaises(ValueError, msg=tag):
                release.label(tag)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=firmware.SCRATCH / "tmp")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.dist = self.base / "dist"
        self.languages = self.base / "lang"
        self.languages.mkdir()
        catalogue = release.ROOT / "firmware/assets/lang/en.txt"
        (self.languages / "en.txt").write_bytes(catalogue.read_bytes())
        self.source = self.base / "source.tar.gz"
        self.source.write_bytes(b"source")
        self.tag = f"v{VERSION}-beta.1"

    def built_image(self, build):
        path = self.base / "built.img"
        path.write_bytes(image(build))
        return path

    def package(self, build="beta.1", tag=None):
        return release.package(self.built_image(build), self.source, self.dist, COMMIT,
                               tag or self.tag, languages_dir=self.languages)

    def verify(self, tag=None):
        return release.verify(self.dist, tag or self.tag, COMMIT, languages_dir=self.languages)

    def add_translations(self):
        (self.languages / "qps.txt").write_text("This test file must never be parsed.")
        for code, name in (("ko", "한국어"), ("de", "Deutsch")):
            (self.languages / f"{code}.txt").write_text(f"STR_LANGUAGE_NAME = {name}\n", encoding="utf-8")

    def write_manifest(self, manifest):
        (self.dist / release.MANIFEST).write_text(json.dumps(manifest))
        sums = self.dist / release.SUMS
        names = [line.split("  ", 1)[1] for line in sums.read_text().splitlines()]
        sums.write_text("".join(f"{release.digest(self.dist / name)}  {name}\n" for name in names))

    def test_a_pre_release_package_names_its_release_and_verifies(self):
        manifest = self.package()
        self.assertIn("ECHOECHO.img", manifest["files"])
        self.assertEqual((manifest["version"], manifest["release"], manifest["build"], manifest["preset"]),
                         (VERSION, VERSION + "-beta.1", "beta.1", "device"))
        self.assertEqual(self.dist.joinpath(STOCK).read_bytes(), self.dist.joinpath(release.IMAGE).read_bytes())
        sums = [line.split("  ")[1] for line in self.dist.joinpath(release.SUMS).read_text().splitlines()]
        self.assertEqual(sums, [release.IMAGE, STOCK, release.source_name(), release.ENGLISH_PACK,
                                release.LANGUAGE_CATALOG, release.MANIFEST])
        self.assertEqual(manifest["language_packs"], [])
        self.assertEqual(self.verify()["commit"], COMMIT)

    def test_packaging_uses_the_repository_languages_by_default(self):
        manifest = release.package(self.built_image("beta.1"), self.source, self.dist, COMMIT, self.tag)
        sources = release.ROOT / "firmware/assets/lang"
        codes = sorted(path.stem for path in sources.glob("*.txt")
                       if path.is_file() and path.stem not in {"en", "qps"})
        self.assertIn("language_packs", manifest)
        self.assertEqual([entry["code"] for entry in manifest["language_packs"]], codes)
        self.assertEqual(release.verify(self.dist, self.tag, COMMIT), manifest)

    def test_packs_are_built_in_code_order_and_listed_in_both_manifests(self):
        self.add_translations()
        manifest = self.package()
        tools = pack_tools(release.ROOT)
        sums = (self.dist / release.SUMS).read_text().splitlines()
        expected = []
        for code, name, needs_extra_font in (("de", "Deutsch", False), ("ko", "한국어", True)):
            filename = f"{code}.pack"
            path = self.dist / filename
            data = path.read_bytes()
            self.assertEqual(data, tools.language_pack(code, self.languages, layout=tools.LAYOUT_HEADER))
            asset = {"sha256": release.digest(path), "bytes": len(data)}
            self.assertEqual(manifest["files"][filename], asset)
            self.assertIn(f"{asset['sha256']}  {filename}", sums)
            expected.append({"code": code, "name": name, "file": filename,
                             "needs_extra_font": needs_extra_font, **asset})
        self.assertEqual(manifest["language_packs"], expected)
        self.assertEqual(list(manifest["files"])[-2:], ["de.pack", "ko.pack"])
        self.assertEqual(self.verify(), manifest)

    def test_the_english_pack_matches_the_exported_catalogue(self):
        self.package()
        catalogue = json.loads((self.dist / release.LANGUAGE_CATALOG).read_text())
        tools = pack_tools(release.ROOT)
        english = tools.pack_info((self.dist / release.ENGLISH_PACK).read_bytes())
        self.assertEqual(english["code"], "en")
        self.assertEqual(english["catalogue_hash"], catalogue["catalogue_hash"])
        self.assertFalse(english["needs_extra_font"])
        self.assertTrue(catalogue["extra_glyph_ranges"])

    def test_a_changed_pack_fails_verification(self):
        self.add_translations()
        self.package()
        pack = self.dist / "de.pack"
        data = pack.read_bytes()
        pack.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
        with self.assertRaisesRegex(ValueError, r"de\.pack: SHA-256"):
            self.verify()

    def test_a_missing_pack_fails_verification(self):
        self.add_translations()
        self.package()
        (self.dist / "de.pack").unlink()
        with self.assertRaisesRegex(ValueError, r"missing.*de\.pack"):
            self.verify()

    def test_an_unexpected_file_fails_verification(self):
        self.package()
        (self.dist / "unexpected.pack").write_bytes(b"extra")
        with self.assertRaisesRegex(ValueError, r"unexpected.*unexpected\.pack"):
            self.verify()

    def test_a_pack_missing_from_checksums_fails_verification(self):
        self.add_translations()
        self.package()
        sums = self.dist / release.SUMS
        sums.write_text("".join(line for line in sums.read_text().splitlines(keepends=True)
                                if not line.endswith("  de.pack\n")))
        with self.assertRaisesRegex(ValueError, r"SHA256SUMS.*missing.*de\.pack"):
            self.verify()

    def test_a_pack_missing_from_the_manifest_files_fails_verification(self):
        self.add_translations()
        manifest = self.package()
        del manifest["files"]["de.pack"]
        self.write_manifest(manifest)
        with self.assertRaisesRegex(ValueError, r"files.*missing.*de\.pack"):
            self.verify()

    def test_language_metadata_that_disagrees_with_a_pack_fails_verification(self):
        self.add_translations()
        manifest = self.package()
        for field, incorrect in (("code", "fr"), ("name", "French"), ("file", "fr.pack"),
                                 ("sha256", "0" * 64), ("bytes", 5), ("needs_extra_font", True)):
            with self.subTest(field=field):
                entry = manifest["language_packs"][0]
                original = entry[field]
                entry[field] = incorrect
                self.write_manifest(manifest)
                with self.assertRaisesRegex(ValueError, "language_packs"):
                    self.verify()
                entry[field] = original

    def test_an_image_built_without_the_label_is_refused(self):
        with self.assertRaisesRegex(ValueError, "ECHO_BUILD_TAG=beta.1"):
            self.package(build="1e1a8e5")

    def test_a_release_package_keeps_the_commit_as_its_build(self):
        manifest = self.package(build="1e1a8e5", tag="v" + VERSION)
        self.assertEqual((manifest["release"], manifest["build"], manifest["preset"]), (VERSION, "1e1a8e5", "release"))
        self.verify(tag="v" + VERSION)

    def test_a_changed_asset_fails_verification(self):
        self.package()
        self.dist.joinpath(STOCK).write_bytes(b"other")
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            self.verify()

    def test_a_package_for_another_tag_fails_verification(self):
        self.package()
        with self.assertRaisesRegex(ValueError, "release"):
            self.verify(tag=f"v{VERSION}-beta.2")

    def test_a_stock_copy_that_differs_from_the_image_fails_verification(self):
        self.package()
        self.dist.joinpath(STOCK).write_bytes(b"other")
        manifest = json.loads(self.dist.joinpath(release.MANIFEST).read_text())
        manifest["files"][STOCK] = {"sha256": release.digest(self.dist / STOCK), "bytes": 5}
        self.write_manifest(manifest)
        with self.assertRaisesRegex(ValueError, "not a copy"):
            self.verify()


class NotesTests(unittest.TestCase):
    def notes(self, text, tag):
        with tempfile.TemporaryDirectory(dir=firmware.SCRATCH / "tmp") as directory:
            path = Path(directory) / "notes.md"
            path.write_text(text)
            return release.notes(tag, path)

    def test_the_section_headed_by_the_release_is_the_body(self):
        text = f"# Notes\n\n## {VERSION}-beta.1 (beta)\n\nNew.\n\n### Known issues\n\n- One.\n\n## Unreleased: later\n\n- Not this.\n"
        self.assertEqual("New.\n\n### Known issues\n\n- One.\n", self.notes(text, f"v{VERSION}-beta.1"))

    def test_a_missing_section_is_refused(self):
        with self.assertRaisesRegex(ValueError, "no section"):
            self.notes(f"## {VERSION}-beta.2\n\nOther.\n", f"v{VERSION}-beta.1")

    def test_the_repository_notes_have_a_section_for_each_listed_release(self):
        headings = [line[3:].split()[0] for line in release.NOTES.read_text().splitlines()
                    if line.startswith("## ") and line[3:4].isdigit()]
        for heading in headings:
            self.assertTrue(release.notes("v" + heading).strip(), heading)


if __name__ == "__main__":
    unittest.main()
