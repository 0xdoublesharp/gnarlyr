import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import firmware
import release_package as release

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
        self.source = self.base / "source.tar.gz"
        self.source.write_bytes(b"source")
        self.tag = f"v{VERSION}-beta.1"

    def package(self, build="beta.1", tag=None):
        path = self.base / "built.img"
        path.write_bytes(image(build))
        return release.package(path, self.source, self.dist, COMMIT, tag or self.tag)

    def verify(self, tag=None):
        return release.verify(self.dist, tag or self.tag, COMMIT)

    def test_a_pre_release_package_names_its_release_and_verifies(self):
        manifest = self.package()
        self.assertIn("ECHOECHO.img", manifest["files"])
        self.assertEqual((manifest["version"], manifest["release"], manifest["build"], manifest["preset"]),
                         (VERSION, VERSION + "-beta.1", "beta.1", "device"))
        self.assertEqual(self.dist.joinpath(STOCK).read_bytes(), self.dist.joinpath(release.IMAGE).read_bytes())
        sums = [line.split("  ")[1] for line in self.dist.joinpath(release.SUMS).read_text().splitlines()]
        self.assertEqual(sums, [release.IMAGE, STOCK, release.source_name(), release.MANIFEST])
        self.assertEqual(self.verify()["commit"], COMMIT)

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
        self.dist.joinpath(release.MANIFEST).write_text(json.dumps(manifest))
        sums = "".join(f"{release.digest(self.dist / name)}  {name}\n" for name in (release.IMAGE, STOCK, release.source_name(), release.MANIFEST))
        self.dist.joinpath(release.SUMS).write_text(sums)
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
