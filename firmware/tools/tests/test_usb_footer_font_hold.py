import re
import unittest
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parents[2]
PREFETCH = FIRMWARE / "src/app/extra_font_usb.c"
FOOTER_HINTS = FIRMWARE / "src/ui/footer_hints.c"
STORAGE_SESSION = FIRMWARE / "platform/device/board/usb_session.c"
HINT = re.compile(r"\{\s*FOOTER_KEY_([A-Z_]+)\s*,\s*(STR_[A-Z0-9_]+)\s*\}")


def table(source, name):
    match = re.search(rf"{name}\[\]\s*=\s*\{{(.*?)\}};", source, re.S)
    if match is None:
        raise AssertionError(f"{name} not found")
    return match.group(1)


def c_string(source, name):
    match = re.search(rf'{name}\[\]\s*=\s*"((?:[^"\\]|\\.)*)";', source)
    if match is None:
        raise AssertionError(f"{name} not found")
    return match.group(1).encode("latin-1").decode("unicode_escape").encode("latin-1").decode()


class UsbFooterFontHold(unittest.TestCase):
    def setUp(self):
        source = PREFETCH.read_text()
        self.small = set(re.findall(r"STR_[A-Z0-9_]+", table(source, "USB_SMALL_TEXT")))
        self.small |= set(re.findall(r"STR_[A-Z0-9_]+", table(source, "USB_STATE_TEXT")))
        self.figures = c_string(source, "USB_FIGURES")

    def hints(self, path):
        pairs = HINT.findall(path.read_text())
        self.assertTrue(pairs, f"no footer hints found in {path.name}")
        return pairs

    def test_every_storage_footer_chip_and_verb_is_held_before_sharing(self):
        for key, verb in self.hints(STORAGE_SESSION):
            with self.subTest(key=key, verb=verb):
                self.assertIn(f"STR_CHIP_{key}", self.small)
                self.assertIn(verb, self.small)

    def test_the_footer_separator_is_held_before_sharing(self):
        separator = c_string(FOOTER_HINTS.read_text(), "SEPARATOR")
        for character in separator:
            self.assertIn(character, self.figures)


if __name__ == "__main__":
    unittest.main()
