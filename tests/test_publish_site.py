from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("publish_site", Path(__file__).parents[1] / "tools/publish_site.py")
publisher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(publisher)


class PublicationTargetTests(unittest.TestCase):
    def target(self, text):
        with mock.patch.object(Path, "read_text", return_value=text):
            return publisher.publication_root()

    def test_target_comes_from_the_existing_read_only_frontend_mount(self):
        self.assertEqual(self.target("  - /srv/hosted-data/example/frontend:/site:ro\n"),
                         "/srv/hosted-data/example/frontend")

    def test_ambiguous_or_writable_frontend_mounts_are_rejected(self):
        for text in ("", "- /srv/hosted-data/example/frontend:/site:rw\n",
                     "- /srv/hosted-data/example/frontend:/site:ro\n" * 2):
            with self.subTest(text=text), self.assertRaises(SystemExit):
                self.target(text)

    def test_targets_outside_one_site_frontend_are_rejected(self):
        for path in ("/srv/other/example/frontend", "/srv/hosted-data/frontend",
                     "/srv/hosted-data/example/../frontend", "/srv/hosted-data/example/runtime"):
            with self.subTest(path=path), self.assertRaises(SystemExit):
                self.target(f"- {path}:/site:ro\n")


class SearchPublicationTests(unittest.TestCase):
    def test_publication_check_includes_browser_search_bundle(self):
        required = {"pagefind/pagefind-component-ui.css", "pagefind/pagefind-component-ui.js",
                    "pagefind/pagefind-entry.json"}

        def index(command, **kwargs):
            root = Path(command[command.index("--site") + 1])
            self.assertIn(f"pagefind@{publisher.PAGEFIND_VERSION}", command)
            self.assertEqual({path.name for path in root.iterdir()},
                             {name for name in publisher.PUBLIC_FILES if name.endswith(".html")})
            self.assertIn(b"<main", (root / "docs.html").read_bytes())
            for name in required:
                target = root / name
                target.parent.mkdir(exist_ok=True)
                target.write_bytes(b"generated search asset")

        out = io.StringIO()
        with (mock.patch.object(sys, "argv", ["publish_site.py", "--check"]),
              mock.patch.object(publisher.subprocess, "run", side_effect=index), redirect_stdout(out)):
            publisher.main()
            files = publisher.public_files(search=True)
        self.assertEqual(required - set(json.loads(out.getvalue())["files"]), set())
        for name, page in files.items():
            if name.endswith(".html") and name != publisher.STUDIO_REDIRECT:
                with self.subTest(page=name):
                    self.assertIn(b'<pagefind-modal-trigger class="site-search">', page)
                    self.assertIn(b'<pagefind-modal></pagefind-modal>', page)
                    self.assertIn(b'src="/pagefind/pagefind-component-ui.js?v=', page)
        self.assertNotIn(b"pagefind", files[publisher.STUDIO_REDIRECT])


class StylesheetPublicationTests(unittest.TestCase):
    def test_css_changes_get_new_urls_without_mutating_source_pages(self):
        scratch = Path("/Volumes/Scratch/SnowskyEcho/tmp")
        self.assertTrue(Path("/Volumes/Scratch").is_mount())
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="site-style-test-", dir=scratch) as folder:
            site = Path(folder)
            source = b'<link rel="stylesheet" href="styles.css"><p>Guide</p>'
            for name in (*publisher.PUBLIC_FILES, publisher.STUDIO_REDIRECT):
                (site / name).parent.mkdir(parents=True, exist_ok=True)
                (site / name).write_bytes(source if name.endswith(".html") else b"asset")
            with mock.patch.object(publisher, "SITE", site):
                first = publisher.public_files()
                (site / "styles.css").write_bytes(b"new CSS")
                second = publisher.public_files()
            for files in (first, second):
                version = hashlib.sha256(files["styles.css"]).hexdigest()[:12].encode()
                for name in publisher.PUBLIC_FILES:
                    if name.endswith(".html") and name != publisher.STUDIO_REDIRECT:
                        self.assertIn(b'href="styles.css?v=' + version + b'"', files[name])
                        self.assertEqual((site / name).read_bytes(), source)
            self.assertNotEqual(first["docs.html"], second["docs.html"])
            self.assertEqual(first["site.js"], second["site.js"])


    def test_regenerated_images_get_new_src_and_full_size_link_urls(self):
        scratch = Path("/Volumes/Scratch/SnowskyEcho/tmp")
        self.assertTrue(Path("/Volumes/Scratch").is_mount())
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="site-image-test-", dir=scratch) as folder:
            site = Path(folder)
            source = b'<img src="assets/screen.png"><a href="assets/screen.png">Full size</a><a href="docs.html">Docs</a>'
            for name in (*publisher.PUBLIC_FILES, publisher.STUDIO_REDIRECT):
                (site / name).parent.mkdir(parents=True, exist_ok=True)
                (site / name).write_bytes(source if name.endswith(".html") else b"asset")
            (site / "assets").mkdir()
            image = site / "assets/screen.png"
            image.write_bytes(b"old image")
            with mock.patch.object(publisher, "SITE", site):
                first = publisher.public_files()
                image.write_bytes(b"new image")
                second = publisher.public_files()
            for files in (first, second):
                version = hashlib.sha256(files["assets/screen.png"]).hexdigest()[:12].encode()
                page = files["docs.html"]
                self.assertIn(b'src="assets/screen.png?v=' + version + b'"', page)
                self.assertIn(b'href="assets/screen.png?v=' + version + b'"', page)
                self.assertIn(b'href="docs.html"', page)
            self.assertNotEqual(first["docs.html"], second["docs.html"])
            self.assertEqual((site / "docs.html").read_bytes(), source)


    def test_companion_page_and_its_images_are_public(self):
        scratch = Path("/Volumes/Scratch/SnowskyEcho/tmp")
        with tempfile.TemporaryDirectory(prefix="site-companion-test-", dir=scratch) as folder:
            site = Path(folder)
            for name in (*publisher.PUBLIC_FILES, publisher.STUDIO_REDIRECT):
                (site / name).parent.mkdir(parents=True, exist_ok=True)
                (site / name).write_bytes(b"public file")
            (site / "README.md").write_bytes(b"private deployment notes")
            for name in ("assets/companion/library-dark.png", "assets/social/companion-123456789abc.png"):
                path = site / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"public image")
            with mock.patch.object(publisher, "SITE", site):
                files = publisher.public_files()
            self.assertIn("companion.html", files)
            self.assertIn("assets/companion/library-dark.png", files)
            self.assertIn("assets/social/companion-123456789abc.png", files)
            self.assertNotIn("README.md", files)


class StudioPublicationTests(unittest.TestCase):
    def test_vice_86_preset_uses_the_firmware_example_and_its_artwork(self):
        with mock.patch.object(sys, "path", [str(publisher.ROOT / "tools"), *sys.path]):
            files = publisher.public_files()
        catalog = json.loads(files["themes-catalog.json"])
        preset = next(theme for theme in catalog["themes"] if theme["id"] == "vice-86")
        source = publisher.ROOT / "examples/themes/vice-86"
        manifest = files[preset["source"]]
        self.assertEqual(manifest, (source / "theme.json").read_bytes())
        prefix = Path(preset["source"]).parent
        for name in {asset["file"] for asset in json.loads(manifest)["assets"]}:
            with self.subTest(asset=name):
                self.assertEqual(files[(prefix / name).as_posix()], (source / name).read_bytes())


if __name__ == "__main__":
    unittest.main()
