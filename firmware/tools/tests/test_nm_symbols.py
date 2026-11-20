import sys
import unittest
from pathlib import Path
from unittest import mock

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import nm_symbols
from nm_symbols import Symbol

SIZED = ("03050000 00000040 t CODE_VECTORS\t/w/firmware/platform/device/board/startup.c:25\n"
         "03004000 00000008 r ENTRY_VECTOR\t/w/firmware/platform/device/board/startup.c:19\n"
         "0305a000 T loop_bench_run\n")
GLOBAL = ("settings_screens.c.obj:\n"
          "         U app_screen_back\n"
          "00000000 T settings_screens_init\n"
          "         w weak_hook\n")
SYSV = ("Symbols from echo_device.elf:\n"
        "\n"
        "Name                  Value   Class        Type         Size     Line  Section\n"
        "\n"
        "first_half          |03050000|   T  |              FUNC|00000040|     |.code\t/w/one.c:10\n"
        "LABEL_TABLE         |03050100|   r  |            OBJECT|00000010|     |.code\n"
        "__aeabi_uldivmod    |03073178|   T  |              FUNC|        |     |.code\n")


class DefaultFormTest(unittest.TestCase):
    def test_sized_lines_carry_address_size_kind_name_and_source_line(self):
        self.assertEqual([
            Symbol(0x03050000, 0x40, "t", "CODE_VECTORS", "/w/firmware/platform/device/board/startup.c", 25),
            Symbol(0x03004000, 8, "r", "ENTRY_VECTOR", "/w/firmware/platform/device/board/startup.c", 19),
            Symbol(0x0305A000, None, "T", "loop_bench_run"),
        ], nm_symbols.parse(SIZED))

    def test_undefined_symbols_have_no_address_and_member_headers_are_skipped(self):
        self.assertEqual([Symbol(None, None, "U", "app_screen_back"),
                          Symbol(0, None, "T", "settings_screens_init"),
                          Symbol(None, None, "w", "weak_hook")], nm_symbols.parse(GLOBAL))

    def test_a_kind_that_is_also_a_hex_digit_is_read_as_the_kind(self):
        self.assertEqual([Symbol(0x20000000, 4, "b", "c"), Symbol(0x20000004, None, "d", "e")],
                         nm_symbols.parse("20000000 00000004 b c\n20000004 d e\n"))

    def test_lines_without_a_name_or_with_stray_text_are_skipped(self):
        self.assertEqual([], nm_symbols.parse("000db7a8 N \n\nnm: warning\nzz T name\n"))

    def test_a_location_without_a_line_number_keeps_the_text_as_the_file(self):
        self.assertEqual(Symbol(16, None, "T", "f", "??", None), nm_symbols.parse_line("00000010 T f\t??"))


class SystemVFormTest(unittest.TestCase):
    def test_rows_carry_the_symbol_type_and_headers_are_skipped(self):
        self.assertEqual([
            Symbol(0x03050000, 0x40, "T", "first_half", "/w/one.c", 10, "FUNC"),
            Symbol(0x03050100, 0x10, "r", "LABEL_TABLE", None, None, "OBJECT"),
            Symbol(0x03073178, None, "T", "__aeabi_uldivmod", None, None, "FUNC"),
        ], nm_symbols.parse(SYSV))


class RunTest(unittest.TestCase):
    def test_options_come_before_the_path_and_output_is_text(self):
        with mock.patch("subprocess.run") as run:
            nm_symbols.run("nm", Path("/b/x.elf"), "-S", "-l")
        run.assert_called_once_with(["nm", "-S", "-l", "/b/x.elf"], capture_output=True, text=True,
                                    check=False)


if __name__ == "__main__":
    unittest.main()
