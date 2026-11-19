import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_lto_logic as check

OVERLAY_BASE = 0x0307A000
MAIN_FUNCTION = 0x03051235


def section(name, content=b"", size=None, address=0, flags=check.SHF_ALLOC, kind=1):
    return check.Section(name, kind, flags, address, content,
                         len(content) if size is None else size)


def symbol(name, index, kind=check.STT_FUNC, binding=check.STB_GLOBAL, value=0, size=4):
    return check.Symbol(name, value, size, binding, kind, index)


def branch_link(source, target):
    offset = target - (source + 4)
    sign = 1 if offset < 0 else 0
    offset &= (1 << 25) - 1
    i1, i2 = (offset >> 23) & 1, (offset >> 22) & 1
    j1, j2 = 1 ^ i1 ^ sign, 1 ^ i2 ^ sign
    first = 0xF000 | (sign << 10) | ((offset >> 12) & 0x3FF)
    second = 0xD000 | (j1 << 13) | (j2 << 11) | ((offset >> 1) & 0x7FF)
    return struct.pack("<HH", first, second)


class Core1Links(unittest.TestCase):
    def test_a_member_named_in_a_core_1_map_is_reported_with_that_program(self):
        maps = {"echo_core1": " .text  0x1 0x2 x/libecho_device_logic.a(clock_governor.c.obj)",
                "echo_core1_mp3": " .text 0x1 0x2 x/libecho_device_logic.a(core1_mp3.c.obj)"}
        self.assertEqual(["echo_core1"], check.core1_links("clock_governor.c.obj", maps))

    def test_a_member_of_another_archive_is_not_a_core_1_link(self):
        maps = {"echo_core1": " .text 0x1 0x2 x/libecho_codec.a(panel.c.obj)"}
        self.assertEqual([], check.core1_links("panel.c.obj", maps))


class StateSymbols(unittest.TestCase):
    def test_variables_in_data_bss_and_common_count_as_state(self):
        elf = check.Elf(
            (section(""), section(".text.f", b"\x00" * 4), section(".bss.table", size=1024),
             section(".data.mode", b"\x01\x00\x00\x00")),
            (symbol("table", 2, check.STT_OBJECT), symbol("mode", 3, check.STT_OBJECT),
             symbol("shared", check.SHN_COMMON, check.STT_OBJECT)))
        self.assertEqual(["mode", "shared", "table"], check.state_symbols(elf))

    def test_a_state_section_without_a_named_object_is_still_reported(self):
        elf = check.Elf((section(""), section(".bss.scratch.0", size=512)), ())
        self.assertEqual([".bss.scratch.0"], check.state_symbols(elf))

    def test_code_and_constant_tables_hold_no_state(self):
        elf = check.Elf((section(""), section(".text.f", b"\x00" * 4),
                         section(".rodata.TABLE", b"\x01" * 8)),
                        (symbol("f", 1), symbol("TABLE", 2, check.STT_OBJECT)))
        self.assertEqual([], check.state_symbols(elf))


class OverlayUses(unittest.TestCase):
    def test_a_branch_with_link_decodes_forwards_and_backwards(self):
        for target in (OVERLAY_BASE + 0x100, OVERLAY_BASE - 0x2000, MAIN_FUNCTION & ~1):
            first, second = struct.unpack("<HH", branch_link(OVERLAY_BASE, target))
            self.assertEqual(target, check.branch_target(OVERLAY_BASE, first, second))

    def test_a_call_into_the_main_programs_copy_is_a_use(self):
        code = b"\x00\xbf" + branch_link(OVERLAY_BASE + 2, MAIN_FUNCTION & ~1)
        overlay = check.Elf((section(".overlay", code, address=OVERLAY_BASE),), ())
        self.assertEqual(["fat_le16"],
                         check.overlay_uses(overlay, {MAIN_FUNCTION & ~1: "fat_le16"}))

    def test_a_function_address_in_a_literal_pool_is_a_use(self):
        code = b"\x00\xbf\x00\xbf" + struct.pack("<I", MAIN_FUNCTION)
        overlay = check.Elf((section(".overlay", code, address=OVERLAY_BASE),), ())
        self.assertEqual(["fat_le16"],
                         check.overlay_uses(overlay, {MAIN_FUNCTION & ~1: "fat_le16"}))

    def test_a_use_of_an_exported_symbol_is_allowed(self):
        code = branch_link(OVERLAY_BASE, MAIN_FUNCTION & ~1)
        overlay = check.Elf((section(".overlay", code, address=OVERLAY_BASE),), ())
        addresses = {MAIN_FUNCTION & ~1: "fat_le16"}
        self.assertEqual([], check.hidden_uses(overlay, addresses, {"fat_le16"}))

    def test_a_use_of_a_symbol_no_import_list_exports_is_reported(self):
        code = branch_link(OVERLAY_BASE, MAIN_FUNCTION & ~1)
        overlay = check.Elf((section(".overlay", code, address=OVERLAY_BASE),), ())
        addresses = {MAIN_FUNCTION & ~1: "sd_card_address"}
        self.assertEqual(["sd_card_address"],
                         check.hidden_uses(overlay, addresses, {"sd_card_rca_argument"}))

    def test_an_overlay_with_its_own_copy_uses_nothing_of_the_main_program(self):
        code = branch_link(OVERLAY_BASE, OVERLAY_BASE + 0x40)
        overlay = check.Elf((section(".overlay", code, address=OVERLAY_BASE),), ())
        self.assertEqual([], check.overlay_uses(overlay, {MAIN_FUNCTION & ~1: "fat_le16"}))

    def test_only_the_objects_global_definitions_are_looked_up_in_the_main_program(self):
        elf = check.Elf((section(""), section(".text.f", b"\x00" * 4)),
                        (symbol("fat_le16", 1), symbol("helper", 1, binding=0),
                         symbol("extern_call", check.SHN_UNDEF)))
        main = check.Elf((section(""), section(".code", b"\x00" * 4)),
                         (symbol("fat_le16", 1, value=MAIN_FUNCTION),
                          symbol("helper", 1, value=MAIN_FUNCTION + 8)))
        self.assertEqual({MAIN_FUNCTION & ~1: "fat_le16"},
                         check.main_addresses(main, check.defined_names(elf)))


class Exported(unittest.TestCase):
    def test_plain_names_and_linker_options_both_name_exports(self):
        with tempfile.TemporaryDirectory() as directory:
            overlay = Path(directory) / "overlay_imports.txt"
            modules = Path(directory) / "module_imports.rsp"
            overlay.write_text("fat_read_entry\napp_theme\n", encoding="utf-8")
            modules.write_text("--require-defined=fat_le16\n", encoding="utf-8")
            self.assertEqual({"fat_read_entry", "app_theme", "fat_le16"},
                             check.read_exported([overlay, modules]))


class LtoObjects(unittest.TestCase):
    def test_each_source_finds_its_object(self):
        objects = ["/b/echo_device_logic.dir/panel.c.obj", "/b/echo_device_logic.dir/pll.c.obj"]
        self.assertEqual([objects[1]], check.lto_objects(objects, ["pll.c"]))

    def test_a_source_without_an_object_stops_the_check(self):
        with self.assertRaises(ValueError):
            check.lto_objects(["/b/panel.c.obj"], ["pll.c"])


if __name__ == "__main__":
    unittest.main()
