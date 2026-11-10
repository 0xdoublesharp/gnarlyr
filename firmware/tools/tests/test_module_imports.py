import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import module_imports

SETTINGS_SCREENS = """
         U choice_screen_init
         U i18n_text
         U __aeabi_uldivmod
00000000 T settings_screens_init
00000000 R SETTINGS_MODULE_ENTRY
         U toast_show
"""

CHOICE_SCREEN = """
00000000 T choice_screen_init
         U i18n_text
         U list_view_init
"""

COVER_FLOW = """
00000000 T cover_flow_screen_init
         U list_view_init
         U image_draw_quad
"""


class ParseTest(unittest.TestCase):
    def test_undefined_and_defined_names_are_told_apart(self):
        defined, undefined = module_imports.parse_nm(SETTINGS_SCREENS)
        self.assertEqual({"settings_screens_init", "SETTINGS_MODULE_ENTRY"}, defined)
        self.assertEqual({"choice_screen_init", "i18n_text", "__aeabi_uldivmod", "toast_show"},
                         undefined)

    def test_a_listing_with_file_headings_is_read_the_same(self):
        text = "\nsettings_screens.c.obj:\n" + SETTINGS_SCREENS
        self.assertEqual(module_imports.parse_nm(SETTINGS_SCREENS), module_imports.parse_nm(text))


class ImportsTest(unittest.TestCase):
    def test_a_module_imports_what_none_of_its_objects_defines(self):
        names = module_imports.imports({"settings": [SETTINGS_SCREENS, CHOICE_SCREEN]})
        self.assertEqual(["i18n_text", "list_view_init", "toast_show"], names)

    def test_the_compilers_own_helpers_are_left_to_the_module_link(self):
        names = module_imports.imports({"settings": [SETTINGS_SCREENS]})
        self.assertNotIn("__aeabi_uldivmod", names)

    def test_the_imports_of_every_module_are_joined_and_sorted(self):
        names = module_imports.imports({"settings": [SETTINGS_SCREENS, CHOICE_SCREEN],
                                        "views": [COVER_FLOW]})
        self.assertEqual(["i18n_text", "image_draw_quad", "list_view_init", "toast_show"], names)

    def test_a_module_that_uses_another_modules_code_is_refused(self):
        with self.assertRaises(module_imports.ModuleImportError) as raised:
            module_imports.imports({"settings": [SETTINGS_SCREENS], "views": [CHOICE_SCREEN]})
        self.assertIn("choice_screen_init", str(raised.exception))
        self.assertIn("module settings", str(raised.exception))

    def test_the_same_source_in_two_modules_is_allowed(self):
        names = module_imports.imports({"settings": [SETTINGS_SCREENS, CHOICE_SCREEN],
                                        "views": [COVER_FLOW, CHOICE_SCREEN]})
        self.assertNotIn("choice_screen_init", names)


class ResponseFileTest(unittest.TestCase):
    def test_every_name_becomes_one_linker_option(self):
        self.assertEqual("--require-defined=i18n_text\n--require-defined=toast_show\n",
                         module_imports.response_file(["i18n_text", "toast_show"]))

    def test_an_unchanged_file_is_left_alone(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "imports.rsp"
            module_imports.write_if_changed(path, "--require-defined=a\n")
            first = path.stat().st_mtime_ns
            module_imports.write_if_changed(path, "--require-defined=a\n")
            self.assertEqual(first, path.stat().st_mtime_ns)
            module_imports.write_if_changed(path, "--require-defined=b\n")
            self.assertEqual("--require-defined=b\n", path.read_text())


if __name__ == "__main__":
    unittest.main()
