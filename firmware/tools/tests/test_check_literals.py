import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
ROOT = TOOLS_DIRECTORY.parent
sys.path.insert(0, str(TOOLS_DIRECTORY))

import check_literals


class TextLiteralTest(unittest.TestCase):
    def test_a_literal_with_a_word_is_text(self):
        source = 'toast_show(toast, "Storage is full", now);\nreturn "On";'
        self.assertEqual({'"Storage is full"', '"On"'}, check_literals.text_literals(source))

    def test_digits_symbols_and_single_letters_are_not_text(self):
        source = 'f("10"); f("%"); f("A"); f("-"); f(" · "); f("{0}"); f("");'
        self.assertEqual(set(), check_literals.text_literals(source))

    def test_escaped_bytes_do_not_make_a_word(self):
        self.assertEqual(set(), check_literals.text_literals('f("\\xE2\\x80\\xA6"); f("\\n\\t");'))
        self.assertEqual({'"vol \\xC2\\xB1"'}, check_literals.text_literals('f("vol \\xC2\\xB1");'))

    def test_includes_are_not_text(self):
        self.assertEqual(set(), check_literals.text_literals('#include "core/i18n.h"\n'))

    def test_assertion_messages_are_not_text_even_over_several_lines(self):
        source = '_Static_assert(sizeof(a) == 4,\n               "the table fits its sector");\n'
        self.assertEqual(set(), check_literals.text_literals(source))

    def test_assembly_and_section_names_are_not_text(self):
        source = ('__asm__ volatile("cpsid i" ::: "memory");\n'
                  'static int x __attribute__((section(".language_ram")));\n')
        self.assertEqual(set(), check_literals.text_literals(source))

    def test_text_after_an_assertion_is_still_found(self):
        source = '_Static_assert(1, "fits");\nreturn "Hello";\n'
        self.assertEqual({'"Hello"'}, check_literals.text_literals(source))

    def test_a_quote_inside_a_literal_does_not_end_it(self):
        self.assertEqual({'"say \\"hello\\" twice"'},
                         check_literals.text_literals('f("say \\"hello\\" twice");'))


class TreeTest(unittest.TestCase):
    def tree(self, files):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        for name, source in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")
        return root

    def run_check(self, files, allowlist):
        root = self.tree({**files, "allow.txt": allowlist})
        return check_literals.main([str(root), "--allowlist", str(root / "allow.txt")])

    def test_only_the_screen_and_board_code_is_checked(self):
        root = self.tree({"src/app/a.c": 'f("Hello");', "src/ui/b.h": 'g("Title");',
                          "src/library/c.c": 'h("Unknown Artist");',
                          "platform/device/board/d.c": 'n("Notice");',
                          "platform/sim/e.c": 'p("usage");', "tests/app/f.c": 'q("Hello");'})
        self.assertEqual({("src/app/a.c", '"Hello"'), ("src/ui/b.h", '"Title"'),
                          ("platform/device/board/d.c", '"Notice"')},
                         check_literals.literals_in(root))

    def test_the_generated_english_table_is_not_checked(self):
        root = self.tree({"src/core/strings_english.h": '[STR_ON] = "On",'})
        self.assertEqual(set(), check_literals.literals_in(root))

    def test_a_new_literal_fails_the_check(self):
        self.assertEqual(1, self.run_check({"src/app/a.c": 'f("Hello");'}, "# nothing\n"))

    def test_a_listed_literal_passes(self):
        self.assertEqual(0, self.run_check({"src/app/a.c": 'f("/settings.bin");'},
                                           '# paths\nsrc/app/a.c: "/settings.bin"\n'))

    def test_a_literal_is_listed_per_file(self):
        self.assertEqual(1, self.run_check({"src/app/a.c": 'f("FLAC");', "src/app/b.c": 'f("FLAC");'},
                                           'src/app/a.c: "FLAC"\n'))

    def test_an_entry_whose_literal_is_gone_fails_the_check(self):
        self.assertEqual(1, self.run_check({"src/app/a.c": "f(STR_HELLO);"},
                                           'src/app/a.c: "Hello"\n'))

    def test_a_line_that_is_not_an_entry_is_an_error(self):
        with self.assertRaises(ValueError):
            self.run_check({"src/app/a.c": ""}, "src/app/a.c Hello\n")


class RepositoryTest(unittest.TestCase):
    def test_every_literal_left_in_the_code_is_listed_with_its_file(self):
        found = check_literals.literals_in(ROOT)
        added, stale = check_literals.compare(found,
                                              check_literals.read_allowlist(check_literals.ALLOWLIST))
        self.assertEqual(([], []), (added, stale))


if __name__ == "__main__":
    unittest.main()
