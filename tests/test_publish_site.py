import importlib.util
from pathlib import Path
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


if __name__ == "__main__":
    unittest.main()
