import sys
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import size_report

MAP = """Archive member included to satisfy reference by file (symbol)

src/app/libecho_app.a(app.c.obj)
                              platform/device/CMakeFiles/echo_device.dir/board/main.c.obj (app_start)

Memory Configuration

Name             Origin             Length             Attributes
DATA_RAM         0x03004000         0x00048000         rw
CODE_RAM         0x03050000         0x0002666c         xr
HIGH_RAM         0x0307a000         0x00018000         rw
*default*        0x00000000         0xffffffff

Linker script and memory map

LOAD platform/device/CMakeFiles/echo_device.dir/board/audio.c.obj

.code           0x03050000     0x1000
 *(.code_vectors)
 .code_vectors  0x03050000       0x40 platform/device/CMakeFiles/echo_device.dir/board/startup.c.obj
 *(.text .text.*)
 .text.pump     0x03050040       0x8c platform/device/CMakeFiles/echo_device.dir/board/audio.c.obj
 .text.hal_audio_play
                0x030500cc       0x40 platform/device/CMakeFiles/echo_device.dir/board/audio.c.obj
                0x030500cc                hal_audio_play
 *fill*         0x0305010c        0x4
 .text.app_step
                0x03050110      0x200 C:\\Users\\x\\Temp\\ccfpNjjI.ltrans0.ltrans.o
 .text.list_view_draw
                0x03050310      0x100 src/ui/libecho_ui.a(list_view.c.obj)
 *(.rodata .rodata.*)
 .rodata.INTER_REGULAR_12_BITMAP
                0x03050410      0x300 src/fonts/libecho_fonts.a(fonts.c.obj)
 .text          0x03050710      0x2ba C:/msys64/clang64/lib/gcc/arm-none-eabi/16.2.0/thumb/v7-m/nofp\\libgcc.a(_udivmoddi4.o)

.data           0x03004000        0x8 load address 0x03051000
 *(.entry_vector)
 .entry_vector  0x03004000        0x8 platform/device/CMakeFiles/echo_device.dir/board/startup.c.obj

.bss            0x03004008    0x20000 load address 0x03051008
 *(.bss .bss.*)
 .bss.music_ring
                0x03004008    0x1b580 platform/device/CMakeFiles/echo_device.dir/board/audio.c.obj
 .bss.app       0x0301f588     0x4a78 C:\\Users\\x\\Temp\\ccfpNjjI.ltrans0.ltrans.o

.noinit         0x03024008       0x30
 .noinit.record 0x03024008       0x30 platform/device/CMakeFiles/echo_device.dir/board/start_info.c.obj
                0x03024038                        device_noinit_end = .

.high_ram       0x0307a000    0x12050
 .high_ram.decoding
                0x0307a000     0xa050 platform/device/CMakeFiles/echo_device.dir/board/audio.c.obj
"""

OVERLAY_MAP = """Memory Configuration

Name             Origin             Length             Attributes
HIGH_RAM         0x0307a000         0x00018000         xr
*default*        0x00000000         0xffffffff

Linker script and memory map

.overlay        0x0307a000    0x14000
 .text.scan     0x0307a010     0x1000 src/library/libecho_library_scan.a(library_scan.c.obj)
"""

USB_OVERLAY_MAP = """Memory Configuration

Name             Origin             Length             Attributes
USB_IMAGE        0x0307a000         0x00010000         xr
HIGH_RAM         0x0307a000         0x00018000         rw
*default*        0x00000000         0xffffffff

Linker script and memory map

.overlay        0x0307a000     0x3000
 .text.usb_poll 0x0307a010      0x400 platform/device/CMakeFiles/echo_usb_overlay.dir/board/usb_port.c.obj

.bss            0x0307d000     0x9000
 .bss.write_buffer
                0x0307d000     0x8000 platform/device/CMakeFiles/echo_usb_overlay.dir/board/usb_session.c.obj
 .bss.source_chunk
                0x03085000        0x4 logic/libecho_device_logic.a(updater.c.obj)
"""

SLOT_REGION = "MODULE_RAM       0x03084400         0x0000dc00         rw\n"
FENCED_MAP = MAP.replace("HIGH_RAM         0x0307a000         0x00018000         rw\n",
                         "HIGH_RAM         0x0307a000         0x0000a400         rw\n" + SLOT_REGION)

MODULE_MAP = """Memory Configuration

Name             Origin             Length             Attributes
MODULE_RAM       0x03084400         0x0000dc00         xrw
*default*        0x00000000         0xffffffff

Linker script and memory map

.overlay        0x03084400     0x2000
 .rodata.SETTINGS_MODULE_ENTRY
                0x03084410        0xc platform/device/CMakeFiles/echo_module_settings_objects.dir/__/__/src/app/modules/settings_module.c.obj
 .text.open_choice
                0x0308441c      0x400 platform/device/CMakeFiles/echo_module_settings_objects.dir/__/__/src/app/settings_screens.c.obj
 .text.choice_screen_init
                0x0308481c      0x100 platform/device/CMakeFiles/echo_module_settings_objects.dir/__/__/src/ui/choice_screen.c.obj

.bss            0x03086400      0x200
 .bss.battery_text
                0x03086400      0x200 platform/device/CMakeFiles/echo_module_settings_objects.dir/__/__/src/app/diagnostics_rows.c.obj
"""

NM = """03050110 00000200 T app_step\tD:/Projects/firmware/src/app/app.c:200
0301f588 00004a78 b app\tD:/Projects/firmware/src/app/app.c:133
030500cc 00000040 T hal_audio_play\tD:/Projects/firmware/platform/device/board/audio.c:50
03050710 000002ba t __udivmoddi4
"""


class ParseMapTest(unittest.TestCase):
    def test_lists_every_placed_input_section_with_its_output(self):
        pieces = size_report.parse_map(MAP)
        by_name = {piece.section: piece for piece in pieces}
        self.assertEqual(by_name[".text.pump"].output, ".code")
        self.assertEqual(by_name[".text.pump"].size, 0x8C)
        self.assertEqual(by_name[".text.hal_audio_play"].address, 0x030500CC)
        self.assertEqual(by_name[".text.app_step"].size, 0x200)
        self.assertEqual(by_name[".bss.music_ring"].output, ".bss")
        self.assertEqual(by_name[".high_ram.decoding"].output, ".high_ram")
        fills = [piece for piece in pieces if piece.section == size_report.FILL]
        self.assertEqual([piece.size for piece in fills], [4])

    def test_output_sizes_come_from_the_section_headers(self):
        sizes = size_report.output_sizes(MAP)
        self.assertEqual(sizes[".code"], 0x1000)
        self.assertEqual(sizes[".data"], 8)
        self.assertEqual(sizes[".bss"], 0x20000)


class NamingTest(unittest.TestCase):
    def test_archive_members_become_library_and_source(self):
        self.assertEqual(size_report.object_name("src/app/libecho_app.a(app.c.obj)"), "app/app.c")
        self.assertEqual(size_report.object_name("src/library/libecho_library.a(library.c.obj)"),
                         "library/library.c")
        self.assertEqual(
            size_report.object_name("platform/device/logic/libecho_device_logic.a(msc.c.obj)"),
            "device_logic/msc.c")

    def test_plain_objects_keep_their_source_path(self):
        self.assertEqual(
            size_report.object_name(
                "platform/device/CMakeFiles/echo_device.dir/board/audio.c.obj"),
            "board/audio.c")

    def test_objects_built_from_another_directory_are_named_like_their_source(self):
        origin = ("platform/device/CMakeFiles/echo_module_views_objects.dir/__/__/src/ui/"
                  "cover_flow.c.obj")
        self.assertEqual(size_report.object_name(origin), "ui/cover_flow.c")

    def test_libgcc_members_are_grouped_under_gcc(self):
        origin = "C:/msys64/clang64/lib/gcc/arm-none-eabi/16.2.0/thumb/v7-m/nofp\\libgcc.a(_udivmoddi4.o)"
        self.assertEqual(size_report.object_name(origin), "gcc/_udivmoddi4")

    def test_lto_pieces_are_attributed_through_nm(self):
        nm_files = size_report.parse_nm(NM)
        pieces = {piece.section: piece for piece in size_report.parse_map(MAP)}
        self.assertEqual(size_report.attribute(pieces[".text.app_step"], nm_files), "app/app.c")
        self.assertEqual(size_report.attribute(pieces[".bss.app"], nm_files), "app/app.c")
        self.assertEqual(size_report.attribute(pieces[".text.pump"], nm_files), "board/audio.c")

    def test_lto_pieces_without_line_information_are_marked(self):
        pieces = {piece.section: piece for piece in size_report.parse_map(MAP)}
        self.assertEqual(size_report.attribute(pieces[".text.app_step"], {}), size_report.UNKNOWN)

    def test_symbol_names_drop_the_section_prefix(self):
        pieces = {piece.section: piece for piece in size_report.parse_map(MAP)}
        self.assertEqual(size_report.symbol_name(pieces[".text.pump"]), "pump")
        self.assertEqual(size_report.symbol_name(pieces[".rodata.INTER_REGULAR_12_BITMAP"]),
                         "INTER_REGULAR_12_BITMAP")


class ReportTest(unittest.TestCase):
    def test_summary_measures_against_each_limit(self):
        lines = size_report.summary(MAP, OVERLAY_MAP)
        self.assertEqual(lines[0], "code window: 4,096 of 157,292 bytes, 153,196 free")
        self.assertEqual(lines[1], "static RAM (.data + .bss + .noinit): 131,128 of 294,912 bytes, "
                                   "163,784 free")
        self.assertEqual(lines[2], "rescan overlay code + .bss: 81,920 of 98,304 bytes, 16,384 free")

    def test_static_ram_is_measured_against_the_boot_rom_limit_when_the_script_names_it(self):
        limited = MAP + "                0x0304a240                        device_data_limit = 0x304a240\n"
        self.assertEqual(size_report.summary(limited)[1],
                         "static RAM (.data + .bss + .noinit): 131,128 of 287,296 bytes, "
                         "156,168 free")

    def test_limits_come_from_the_memory_regions_of_each_map(self):
        smaller = OVERLAY_MAP.replace("0x00018000", "0x00015000")
        self.assertEqual(size_report.summary(MAP, smaller)[2],
                         "rescan overlay code + .bss: 81,920 of 86,016 bytes, 4,096 free")

    def test_rescan_overlay_counts_zeroed_state_against_its_window(self):
        zeroed = OVERLAY_MAP + "\n.bss            0x0308e000     0x400\n"
        self.assertEqual(size_report.summary(MAP, zeroed)[2],
                         "rescan overlay code + .bss: 82,944 of 98,304 bytes, 15,360 free")
        over = zeroed.replace("0x400", "0x4004")
        self.assertIn("OVER BY 4", size_report.summary(MAP, over)[2])

    def test_summary_measures_the_usb_overlay_in_the_image_and_with_its_zeroed_memory(self):
        lines = size_report.summary(MAP, OVERLAY_MAP, USB_OVERLAY_MAP)
        self.assertEqual(lines[3], "USB overlay code: 12,288 of 65,536 bytes, 53,248 free")
        self.assertEqual(lines[4], "USB overlay code + .bss: 49,152 of 98,304 bytes, 49,152 free")

    def test_the_usb_overlay_limits_come_from_its_own_memory_regions(self):
        smaller = USB_OVERLAY_MAP.replace("0x00010000", "0x00004000").replace("0x00018000",
                                                                              "0x0000c000")
        lines = size_report.summary(MAP, OVERLAY_MAP, smaller)
        self.assertEqual(lines[3], "USB overlay code: 12,288 of 16,384 bytes, 4,096 free")
        self.assertEqual(lines[4], "USB overlay code + .bss: 49,152 of 49,152 bytes, 0 free")

    def test_a_usb_overlay_map_without_its_image_region_is_refused(self):
        with self.assertRaises(ValueError):
            size_report.summary(MAP, OVERLAY_MAP, USB_OVERLAY_MAP.replace("USB_IMAGE", "OTHER_RAM"))

    def test_variables_only_the_overlay_has_are_not_split(self):
        self.assertEqual([], size_report.split_state(MAP, USB_OVERLAY_MAP))

    def test_a_library_object_with_variables_in_both_programs_is_named(self):
        both = MAP.replace(" .bss.app       0x0301f588", """ .bss.target_chunk
                0x0301f584        0x4 platform/device/logic/libecho_device_logic.a(updater.c.obj)
 .bss.app       0x0301f588""")
        self.assertEqual(["libecho_device_logic.a(updater.c.obj)"],
                         size_report.split_state(both, USB_OVERLAY_MAP))

    def test_summary_measures_the_largest_language_pack_against_its_memory(self):
        with_region = MAP.replace("HIGH_RAM         0x0307a000         0x00018000         rw\n",
                                  "HIGH_RAM         0x0307a000         0x00018000         rw\n"
                                  "LANGUAGE_RAM     0x0309d000         0x00002000         rw\n")
        lines = size_report.summary(with_region, packs=[("en", 4501), ("de", 5120)])
        self.assertEqual(lines[-1], "language pack (de): 5,120 of 8,192 bytes, 3,072 free")
        over = size_report.summary(with_region, packs=[("ru", 8200)])
        self.assertIn("OVER BY 8", over[-1])

    def test_a_fenced_decoder_workspace_is_measured_against_the_memory_below_the_module_slot(self):
        fenced = MAP.replace("HIGH_RAM         0x0307a000         0x00018000         rw\n",
                             "HIGH_RAM         0x0307a000         0x00012400         rw\n"
                             "MODULE_RAM       0x0308c400         0x00005c00         rw\n")
        self.assertEqual(size_report.summary(fenced)[2],
                         "decoder workspace: 73,808 of 74,752 bytes, 944 free")

    def test_summary_measures_each_module_with_its_zeroed_memory_against_the_slot(self):
        lines = size_report.summary(FENCED_MAP, modules=[("settings", MODULE_MAP)])
        self.assertEqual(lines[2], "module settings (code 8,192 + .bss 512): 8,704 of 56,320 "
                                   "bytes, 47,616 free")
        self.assertTrue(lines[3].startswith("decoder workspace"))

    def test_a_module_that_fits_its_slot_has_no_problem(self):
        self.assertEqual([], size_report.module_problems(FENCED_MAP, [("settings", MODULE_MAP)]))

    def test_the_audio_module_has_a_separate_bounded_stateless_slot(self):
        device = FENCED_MAP.replace(SLOT_REGION, SLOT_REGION +
                                   "DSP_RAM          0x03048000         0x00002000         rx\n")
        audio = """Memory Configuration

Name             Origin             Length             Attributes
MODULE_RAM       0x03048000         0x00002000         rx
*default*        0x00000000         0xffffffff

Linker script and memory map

.overlay        0x03048000     0x1000
.bss            0x03049000        0x0
"""
        self.assertEqual([], size_report.module_problems(device, [("dsp", audio)]))
        self.assertTrue(any("4,096 of 8,192" in line for line in
                            size_report.summary(device, modules=[("dsp", audio)])))
        self.assertTrue(size_report.module_problems(FENCED_MAP, [("dsp", audio)]))
        self.assertTrue(size_report.module_problems(device, [("settings", audio)]))
        self.assertTrue(size_report.module_problems(device, [("dsp", MODULE_MAP)]))
        stateful = audio.replace("0x03049000        0x0", "0x03049000        0x4")
        self.assertIn("the audio module must keep all state in its caller",
                      size_report.module_problems(device, [("dsp", stateful)]))
        grown = audio.replace("0x03048000     0x1000", "0x03048000     0x2004")
        self.assertIn("module dsp is 4 bytes too large for the module slot",
                      size_report.module_problems(device, [("dsp", grown)]))

    def test_processing_uses_its_own_window_and_bounds_private_state(self):
        device = FENCED_MAP.replace(SLOT_REGION, SLOT_REGION +
                                    "PROCESS_RAM      0x0308f000         0x00003000         rx\n")
        process = MODULE_MAP.replace("0x03084400", "0x0308f000").replace(
            "0x0000dc00", "0x00003000").replace("0x03086400", "0x03091000")
        process = process.replace(".bss            0x03091000      0x200",
                                   ".bss            0x03091000      0x0")
        self.assertEqual([], size_report.module_problems(device, [("process", process)]))
        self.assertTrue(size_report.module_problems(device, [("settings", process)]))
        stateful = process.replace(".bss            0x03091000      0x0",
                                   ".bss            0x03091000      0x4")
        self.assertEqual([], size_report.module_problems(device, [("process", stateful)]))
        grown = stateful.replace(".bss            0x03091000      0x4",
                                ".bss            0x03091000   0x1004")
        self.assertIn("module process is 4 bytes too large for the module slot",
                      size_report.module_problems(device, [("process", grown)]))

    def test_the_extension_module_uses_its_own_window_and_keeps_state_in_the_main_program(self):
        device = FENCED_MAP.replace(SLOT_REGION, SLOT_REGION +
                                    "EXTENSION_RAM    0x0304a000         0x00002000         rx\n")
        extension = MODULE_MAP.replace("0x03084400", "0x0304a000").replace(
            "0x0000dc00", "0x00002000").replace("0x03086400", "0x0304b000")
        extension = extension.replace(".bss            0x0304b000      0x200",
                                      ".bss            0x0304b000      0x0")
        self.assertEqual([], size_report.module_problems(device, [("extension", extension)]))
        self.assertTrue(size_report.module_problems(device, [("settings", extension)]))
        self.assertTrue(size_report.module_problems(FENCED_MAP, [("extension", extension)]))
        stateful = extension.replace(".bss            0x0304b000      0x0",
                                     ".bss            0x0304b000      0x4")
        self.assertIn("the extension module must keep all state in the main program",
                      size_report.module_problems(device, [("extension", stateful)]))

    def test_a_module_larger_than_the_slot_is_a_problem(self):
        grown = MODULE_MAP.replace(".bss            0x03086400      0x200",
                                   ".bss            0x03086400     0xbd00")
        problems = size_report.module_problems(FENCED_MAP, [("settings", grown)])
        self.assertEqual(["module settings is 256 bytes too large for the module slot"], problems)
        self.assertIn("OVER BY 256", size_report.summary(FENCED_MAP,
                                                         modules=[("settings", grown)])[2])

    def test_a_module_linked_for_another_slot_is_a_problem(self):
        moved = MODULE_MAP.replace("0x03084400         0x0000dc00", "0x03084000         0x0000e000")
        for module_map, device_map in ((moved, FENCED_MAP), (MODULE_MAP, MAP)):
            problems = size_report.module_problems(device_map, [("settings", module_map)])
            self.assertEqual(["module settings is not linked for the main program's module slot"],
                             problems)

    def test_the_command_fails_when_a_module_is_too_large(self):
        import contextlib
        import io
        import tempfile
        grown = MODULE_MAP.replace(".bss            0x03086400      0x200",
                                   ".bss            0x03086400     0xbd00")
        with tempfile.TemporaryDirectory() as directory:
            paths = {}
            for name, text in (("device", FENCED_MAP), ("fits", MODULE_MAP), ("grown", grown)):
                paths[name] = Path(directory) / (name + ".map")
                paths[name].write_text(text)
            results = []
            for name in ("fits", "grown"):
                with contextlib.redirect_stdout(io.StringIO()), \
                        contextlib.redirect_stderr(io.StringIO()):
                    results.append(size_report.main(["--map", str(paths["device"]), "--summary",
                                                     "--module-map", f"settings={paths[name]}"]))
        self.assertEqual([0, 1], results)

    def test_the_report_lists_what_fills_each_module(self):
        text = size_report.report(FENCED_MAP, modules=[("settings", MODULE_MAP)])
        lines = text.splitlines()
        table = lines[lines.index("module settings by object:") + 2:]
        self.assertTrue(table[0].startswith("app/settings_screens.c"), table[0])
        self.assertIn("1,024", table[0])
        rows = next(line for line in table if "diagnostics_rows" in line)
        self.assertTrue(rows.rstrip().endswith("512"), rows)

    def test_without_packs_the_summary_has_no_language_line(self):
        self.assertEqual(2, len(size_report.summary(MAP)))

    def test_packs_need_the_language_memory_region(self):
        with self.assertRaises(ValueError):
            size_report.summary(MAP, packs=[("en", 4501)])

    def test_summary_flags_a_limit_that_is_over(self):
        over = MAP.replace(".code           0x03050000     0x1000",
                           ".code           0x03050000    0x26700")
        self.assertIn("OVER BY 148", size_report.summary(over)[0])

    def test_report_groups_code_by_object_and_lists_the_biggest_symbols(self):
        text = size_report.report(MAP, OVERLAY_MAP, NM, top=3, objects=10)
        self.assertIn("code window: 4,096 of 157,292 bytes, 153,196 free", text)
        self.assertIn("code window: .text 1,670, .rodata 768, fill 4, vectors and the rest 1,654",
                      text)
        lines = text.splitlines()
        objects = lines[lines.index("largest objects (of 7):") + 2:]
        self.assertTrue(objects[0].startswith("fonts/fonts.c"), objects[0])
        self.assertIn("768", objects[0])
        self.assertTrue(objects[1].startswith("gcc/_udivmoddi4"), objects[1])
        app = next(line for line in objects if line.startswith("app/app.c"))
        self.assertIn("512", app)
        self.assertIn("19,064", app)
        self.assertIn("INTER_REGULAR_12_BITMAP", text)
        self.assertIn("music_ring", text)
        self.assertIn("112,000", text)
        self.assertNotIn(size_report.FILL, text.split("largest objects")[1])


if __name__ == "__main__":
    unittest.main()
