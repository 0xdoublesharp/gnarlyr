import hashlib
import importlib.util
from pathlib import Path
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


class StylesheetPublicationTests(unittest.TestCase):
    def test_css_changes_get_new_urls_without_mutating_source_pages(self):
        scratch = Path("/Volumes/Scratch/SnowskyEcho/tmp")
        self.assertTrue(Path("/Volumes/Scratch").is_mount())
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="site-style-test-", dir=scratch) as folder:
            site = Path(folder)
            source = b'<link rel="stylesheet" href="styles.css"><p>Guide</p>'
            for name in publisher.PUBLIC_FILES:
                (site / name).write_bytes(source if name.endswith(".html") else b"asset")
            with mock.patch.object(publisher, "SITE", site):
                first = publisher.public_files()
                (site / "styles.css").write_bytes(b"new CSS")
                second = publisher.public_files()
            for files in (first, second):
                version = hashlib.sha256(files["styles.css"]).hexdigest()[:12].encode()
                for name in publisher.PUBLIC_FILES:
                    if name.endswith(".html"):
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
            for name in publisher.PUBLIC_FILES:
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


if __name__ == "__main__":
    unittest.main()
