"""Reports what fills the device code window, from the linker maps and the ELF.

The map gives every input section the linker placed (one per function or table with
-ffunction-sections and -fdata-sections) and which object it came from. Objects that went
through link-time optimisation all appear as one `ltrans` object, so those pieces are
attributed through the ELF's line information instead (`nm -S -l`, which needs -g).
"""
import argparse
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

CODE_REGION = "CODE_RAM"
DATA_REGION = "DATA_RAM"
OVERLAY_REGION = "HIGH_RAM"
WORKSPACE_REGION = "HIGH_RAM"
WORKSPACE_SECTION = ".high_ram"
MODULE_REGION = "MODULE_RAM"
MODULE_WINDOWS = {"dsp": "DSP_RAM", "ogg": "DSP_RAM", "process": "PROCESS_RAM",
                  "extension": "EXTENSION_RAM"}
USB_IMAGE_REGION = "USB_IMAGE"
LANGUAGE_REGION = "LANGUAGE_RAM"
STATE_SECTIONS = (".data", ".bss", ".noinit")
DATA_END_SYMBOL = "device_noinit_end"
DATA_LIMIT_SYMBOL = "device_data_limit"
CODE_SECTIONS = (".code",)
RAM_SECTIONS = (".data", ".bss", ".noinit")
INPUT_LINE = re.compile(r"^ (\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")
INPUT_NAME_LINE = re.compile(r"^ (\S+)$")
INPUT_TAIL_LINE = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")
FILL_LINE = re.compile(r"^ \*fill\*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)")
OUTPUT_LINE = re.compile(r"^(\.[\w.]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)")
MEMBER = re.compile(r"^(.*?)\(([^()]+)\)$")
NM_LINE = re.compile(r"^([0-9a-f]+) ([0-9a-f]+) (\S) (\S+)(?:\t(.*):(\d+))?$")
REGION_LINE = re.compile(r"^(\w+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+\w+$")
SYMBOL_LINE = re.compile(r"^\s+0x([0-9a-f]+)\s+(\w+) = ")
REGIONS_START = "Memory Configuration"
MAP_START = "Linker script and memory map"
FILL = "*fill*"
LTO_OBJECT = "ltrans"
UNKNOWN = "lto/unattributed"
SOURCE_PREFIXES = ("src/", "platform/device/", "platform/", "third_party/")


@dataclass(frozen=True)
class Piece:
    section: str
    output: str
    address: int
    size: int
    origin: str


def parse_map(text):
    """The input sections the linker placed, as Pieces, in map order."""
    pieces = []
    output = None
    pending = None
    started = False
    for line in text.splitlines():
        if not started:
            started = line.startswith(MAP_START)
            continue
        match = OUTPUT_LINE.match(line)
        if match:
            output = match.group(1)
            pending = None
            continue
        if output is None:
            continue
        if not line.startswith(" ") or line.startswith(" *("):
            pending = None
            continue
        match = FILL_LINE.match(line)
        if match:
            pieces.append(Piece(FILL, output, int(match.group(1), 16), int(match.group(2), 16),
                                FILL))
            continue
        match = INPUT_LINE.match(line)
        if match:
            pieces.append(Piece(match.group(1), output, int(match.group(2), 16),
                                int(match.group(3), 16), match.group(4).strip()))
            pending = None
            continue
        match = INPUT_NAME_LINE.match(line)
        if match and match.group(1).startswith("."):
            pending = match.group(1)
            continue
        match = INPUT_TAIL_LINE.match(line)
        if match and pending is not None:
            pieces.append(Piece(pending, output, int(match.group(1), 16), int(match.group(2), 16),
                                match.group(3).strip()))
        pending = None
    return pieces


def output_sizes(text):
    """The output sections' sizes as the map states them."""
    sizes = {}
    started = False
    for line in text.splitlines():
        if not started:
            started = line.startswith(MAP_START)
            continue
        match = OUTPUT_LINE.match(line)
        if match:
            sizes[match.group(1)] = int(match.group(3), 16)
    return sizes


def memory_regions(text):
    """The linker script's MEMORY regions as the map states them: name -> (origin, length)."""
    regions = {}
    started = False
    for line in text.splitlines():
        if line.startswith(MAP_START):
            break
        if not started:
            started = line.startswith(REGIONS_START)
            continue
        match = REGION_LINE.match(line)
        if match:
            regions[match.group(1)] = (int(match.group(2), 16), int(match.group(3), 16))
    return regions


def linker_symbols(text):
    """The values the linker script assigned to its own symbols: name -> address."""
    symbols = {}
    for line in text.splitlines():
        match = SYMBOL_LINE.match(line)
        if match:
            symbols[match.group(2)] = int(match.group(1), 16)
    return symbols


def region_length(regions, name):
    if name not in regions:
        raise ValueError(f"the map has no {name} memory region")
    return regions[name][1]


def source_name(path):
    """`D:/.../src/app/player.c` -> `app/player.c`; a device file -> `board/main.c`."""
    name = path.replace("\\", "/")
    for prefix in SOURCE_PREFIXES:
        marker = "/" + prefix
        if marker in name:
            return name.split(marker, 1)[1]
    return Path(name).name


def inside_source_tree(path):
    """`__/__/src/app/player.c.obj`, an object built from another directory -> `app/player.c.obj`."""
    while path.startswith("__/"):
        path = path[3:]
    for prefix in SOURCE_PREFIXES:
        if path.startswith(prefix):
            return path[len(prefix):]
    return path


def object_name(origin):
    """`libecho_app.a(player.c.obj)` -> `app/player.c`; a plain object -> its source path."""
    if origin == FILL:
        return FILL
    name = origin.replace("\\", "/")
    match = MEMBER.match(name)
    if match:
        archive = Path(match.group(1)).stem
        for prefix in ("libecho_", "lib"):
            if archive.startswith(prefix):
                archive = archive[len(prefix):]
                break
        return f"{archive}/{Path(match.group(2)).stem}"
    if ".dir/" in name:
        return Path(inside_source_tree(name.split(".dir/", 1)[1])).with_suffix("").as_posix()
    return Path(name).stem


def parse_nm(text):
    """Symbol address -> source file, from `nm -S -l` output."""
    files = {}
    for line in text.splitlines():
        match = NM_LINE.match(line)
        if match and match.group(5):
            files[int(match.group(1), 16)] = match.group(5)
    return files


def attribute(piece, nm_files):
    if LTO_OBJECT not in piece.origin:
        return object_name(piece.origin)
    address = piece.address
    for candidate in (address, address | 1, address & ~1):
        if candidate in nm_files:
            return source_name(nm_files[candidate])
    return UNKNOWN


def symbol_name(piece):
    for prefix in (".text.", ".rodata.", ".bss.", ".data.", ".noinit."):
        if piece.section.startswith(prefix):
            return piece.section[len(prefix):]
    return piece.section


def in_sections(pieces, outputs):
    return [piece for piece in pieces if piece.output in outputs]


def format_table(rows, headers):
    widths = [max(len(str(row[index])) for row in [headers, *rows]) for index in range(len(headers))]
    lines = []
    for row in [headers, *rows]:
        cells = [str(cell).rjust(width) if index else str(cell).ljust(width)
                 for index, (cell, width) in enumerate(zip(row, widths))]
        lines.append("  ".join(cells).rstrip())
    return lines


def limit_line(label, used, limit):
    free = limit - used
    state = "" if free >= 0 else f"  OVER BY {-free:,}"
    return f"{label}: {used:,} of {limit:,} bytes, {free:,} free{state}"


def data_line(device_map, regions):
    """Static RAM against the highest end the boot ROM is known to accept."""
    symbols = linker_symbols(device_map)
    origin = regions[DATA_REGION][0]
    sizes = output_sizes(device_map)
    used = symbols.get(DATA_END_SYMBOL, origin + sum(sizes.get(name, 0) for name in RAM_SECTIONS))
    limit = symbols.get(DATA_LIMIT_SYMBOL, origin + regions[DATA_REGION][1])
    return limit_line("static RAM (.data + .bss + .noinit)", used - origin, limit - origin)


def usb_overlay_lines(usb_overlay_map):
    """The USB overlay against its space in the image and, with its zeroed memory, the window."""
    sizes = output_sizes(usb_overlay_map)
    regions = memory_regions(usb_overlay_map)
    code = sizes.get(".overlay", 0)
    return [limit_line("USB overlay code", code, region_length(regions, USB_IMAGE_REGION)),
            limit_line("USB overlay code + .bss", code + sizes.get(".bss", 0),
                       region_length(regions, OVERLAY_REGION))]


def split_state(device_map, overlay_map):
    """Library objects that keep variables in both the main program and an overlay.

    An overlay linked against the main program's symbols still gets its own copy of a library
    object's variables when it pulls that object in, so the two programs would each see a
    different one.
    """
    def stateful(text):
        return {piece.origin.replace("\\", "/").rsplit("/", 1)[-1]
                for piece in in_sections(parse_map(text), STATE_SECTIONS)
                if piece.size and MEMBER.match(piece.origin)}

    return sorted(stateful(device_map) & stateful(overlay_map))


def module_extent(module_map):
    """(code, zeroed memory) of a module, from its own map."""
    sizes = output_sizes(module_map)
    return sizes.get(".overlay", 0), sizes.get(".bss", 0)


def module_lines(modules):
    """Each module with its zeroed memory against the slot it is linked for."""
    lines = []
    for name, module_map in modules:
        code, zeroed = module_extent(module_map)
        slot = region_length(memory_regions(module_map), MODULE_REGION)
        lines.append(limit_line(f"module {name} (code {code:,} + .bss {zeroed:,})", code + zeroed,
                                slot))
    return lines


def module_problems(device_map, modules):
    """What makes a module unusable: another slot than the main program's, or more than it holds."""
    problems = []
    regions = memory_regions(device_map)
    for name, module_map in modules:
        region = MODULE_WINDOWS.get(name, MODULE_REGION)
        slot = regions.get(region)
        linked = memory_regions(module_map).get(MODULE_REGION)
        if slot is None or linked != slot:
            problems.append(f"module {name} is not linked for the main program's module slot")
            continue
        code, zeroed = module_extent(module_map)
        if name in ("dsp", "ogg", "process") and zeroed:
            problems.append("the audio module must keep all state in its caller")
        if name == "extension" and zeroed:
            problems.append("the extension module must keep all state in the main program")
        if code + zeroed > slot[1]:
            problems.append(f"module {name} is {code + zeroed - slot[1]:,} bytes too large for "
                            "the module slot")
        split = split_state(device_map, module_map)
        if split:
            problems.append(f"the main program and module {name} each have their own variables "
                            "of " + ", ".join(split))
    return problems


def workspace_line(device_map, regions):
    """The decoder workspace against the part of high RAM below the module slot."""
    used = output_sizes(device_map).get(WORKSPACE_SECTION, 0)
    return limit_line("decoder workspace", used, region_length(regions, WORKSPACE_REGION))


def language_line(regions, packs):
    """The largest language pack against the memory the player loads one pack into."""
    name, size = max(packs, key=lambda pack: pack[1])
    return limit_line(f"language pack ({name})", size, region_length(regions, LANGUAGE_REGION))


def summary(device_map, overlay_map=None, usb_overlay_map=None, packs=(), modules=()):
    regions = memory_regions(device_map)
    code = output_sizes(device_map).get(".code", 0)
    lines = [limit_line("code window", code, region_length(regions, CODE_REGION)),
             data_line(device_map, regions)]
    if overlay_map is not None:
        overlay_code, overlay_zeroed = module_extent(overlay_map)
        overlay = overlay_code + overlay_zeroed
        capacity = region_length(memory_regions(overlay_map), OVERLAY_REGION)
        lines.append(limit_line("rescan overlay code + .bss", overlay, capacity))
    if usb_overlay_map is not None:
        lines += usb_overlay_lines(usb_overlay_map)
    lines += module_lines(modules)
    if MODULE_REGION in regions:
        lines.append(workspace_line(device_map, regions))
    if packs:
        lines.append(language_line(regions, packs))
    return lines


@dataclass(frozen=True)
class Budget:
    preset: str
    name: str
    used: int
    limit: int
    floor: int = 0

    @property
    def ok(self):
        return self.limit - self.used >= self.floor


BUDGET_PRESETS = ("release", "device")
RESIDENT_CODE_FLOORS = {"release": 4096, "device": 1024}
BUDGET_MODULES = {"views", "settings", "library", "listening", "dsp", "ogg", "process", "extension",
                  "utilities"}
BUDGET_MAPS = {"echo_usb_overlay.map", "echo_rescan_overlay.map", "echo_core1.map", "echo_core1_mp3.map",
               "echo_core1_m4a.map", "echo_core1_vorbis.map", "echo_core1_opus.map"}
CORE1_SCRIPTS = Path(__file__).resolve().parents[1] / "platform/device/core1"
BUDGET_HEADERS = ("Preset", "Region / slot", "Used", "Limit", "Free", "Floor", "Result")


def resident_rows(device, preset):
    regions = memory_regions(device)
    sizes = output_sizes(device)
    symbols = linker_symbols(device)
    origin, capacity = regions[DATA_REGION]
    return [Budget(preset, "resident code", sizes[".code"], regions[CODE_REGION][1],
                   RESIDENT_CODE_FLOORS[preset]),
            Budget(preset, "static RAM", symbols[DATA_END_SYMBOL] - origin,
                   symbols.get(DATA_LIMIT_SYMBOL, origin + capacity) - origin),
            Budget(preset, "decoder workspace", sizes[WORKSPACE_SECTION], regions[WORKSPACE_REGION][1])]


def overlay_rows(path, text, preset):
    memory = memory_regions(text)
    rows = [Budget(preset, path.stem.removeprefix("echo_"), sum(module_extent(text)),
                   memory[OVERLAY_REGION][1])]
    if USB_IMAGE_REGION in memory:
        rows.append(Budget(preset, "USB image slot", output_sizes(text)[".overlay"],
                           memory[USB_IMAGE_REGION][1]))
    return rows


def core1_rows(path, text, preset):
    import make_image

    name = path.stem.removeprefix("echo_")
    script = CORE1_SCRIPTS / f"{name}.ld"
    match = re.search(r"ASSERT\(core1_bss_end <= (0x[0-9A-Fa-f]+)", script.read_text())
    if match is None:
        raise ValueError(f"Missing core 1 stack boundary: {script}")
    sections = output_sizes(text)
    static_limit = int(match[1], 16) - memory_regions(text)["CORE1_DATA"][0]
    code_limit = {"core1_vorbis": make_image.CORE1_VORBIS_CAPACITY,
                  "core1_m4a": make_image.CORE1_M4A_CAPACITY}.get(name, make_image.CORE1_MODULE_CAPACITY)
    return [Budget(preset, f"{name} image bank", sections[".text"], code_limit),
            Budget(preset, f"{name} static RAM", sections.get(".bss", 0) + sections.get(".data", 0),
                   static_limit)]


def check_budget_inputs(directory, device, modules):
    present = {path.name for path in directory.glob("*.map")}
    if {name for name, _ in modules} != BUDGET_MODULES or not BUDGET_MAPS <= present:
        raise ValueError(f"Incomplete size inputs in {directory}; rebuild both presets")
    if any(split_state(device, text) for _, text in modules):
        raise ValueError("Split module state")
    problems = module_problems(device, modules)
    if problems:
        raise ValueError("; ".join(problems))


def budget_rows(build, preset):
    directory = build / "platform/device"
    device = (directory / "echo_device.map").read_text()
    rows = resident_rows(device, preset)
    modules = []
    for path in sorted(directory.glob("*.map")):
        text = path.read_text()
        if path.stem.startswith("echo_module_"):
            name = path.stem.removeprefix("echo_module_")
            modules.append((name, text))
            rows.append(Budget(preset, f"module {name}", sum(module_extent(text)),
                               memory_regions(text)[MODULE_REGION][1]))
        elif path.stem in ("echo_rescan_overlay", "echo_usb_overlay"):
            rows += overlay_rows(path, text, preset)
        elif path.stem.startswith("echo_core1"):
            rows += core1_rows(path, text, preset)
    check_budget_inputs(directory, device, modules)
    packs = sorted((build / "lang").glob("*.pack"))
    if not packs:
        raise ValueError(f"Missing language packs in {build}")
    language = memory_regions(device)[LANGUAGE_REGION][1]
    return rows + [Budget(preset, f"language {path.stem}", path.stat().st_size, language) for path in packs]


def budget_table(rows):
    table = [(row.preset, row.name, f"{row.used:,}", f"{row.limit:,}", f"{row.limit - row.used:,}",
              f"{row.floor:,}", "PASS" if row.ok else "FAIL") for row in rows]
    return format_table(table, BUDGET_HEADERS)


def module_report(name, module_map):
    """What fills one module, by object."""
    by_object = defaultdict(lambda: [0, 0])
    for piece in parse_map(module_map):
        if piece.section != FILL and piece.output in (".overlay", ".bss"):
            by_object[object_name(piece.origin)][piece.output == ".bss"] += piece.size
    rows = [(origin, f"{code:,}", f"{zeroed:,}") for origin, (code, zeroed) in
            sorted(by_object.items(), key=lambda item: (-item[1][0], item[0]))]
    return [f"module {name} by object:", *format_table(rows, ("object", "code", ".bss")), ""]


def report(device_map, overlay_map=None, nm_text="", top=25, objects=40, usb_overlay_map=None,
           packs=(), modules=()):
    pieces = parse_map(device_map)
    nm_files = parse_nm(nm_text)
    lines = summary(device_map, overlay_map, usb_overlay_map, packs, modules)
    lines.append("")
    code_pieces = in_sections(pieces, CODE_SECTIONS)
    ram_pieces = in_sections(pieces, RAM_SECTIONS)
    text = sum(piece.size for piece in code_pieces if piece.section.startswith(".text"))
    rodata = sum(piece.size for piece in code_pieces if piece.section.startswith(".rodata"))
    fill = sum(piece.size for piece in code_pieces if piece.section == FILL)
    code = output_sizes(device_map).get(".code", 0)
    lines.append(f"code window: .text {text:,}, .rodata {rodata:,}, fill {fill:,}, "
                 f"vectors and the rest {code - text - rodata - fill:,}")
    lines.append("")
    by_object_code = defaultdict(int)
    by_object_ram = defaultdict(int)
    for piece in code_pieces:
        by_object_code[attribute(piece, nm_files)] += piece.size
    for piece in ram_pieces:
        by_object_ram[attribute(piece, nm_files)] += piece.size
    names = sorted((set(by_object_code) | set(by_object_ram)) - {FILL},
                   key=lambda name: (-by_object_code.get(name, 0), name))
    rows = [(name, f"{by_object_code.get(name, 0):,}", f"{by_object_ram.get(name, 0):,}")
            for name in names[:objects]]
    lines.append(f"largest objects (of {len(names)}):")
    lines += format_table(rows, ("object", "code", "ram"))
    lines.append("")
    by_library = defaultdict(int)
    for name, size in by_object_code.items():
        if name != FILL:
            by_library[name.split("/")[0]] += size
    rows = [(name, f"{size:,}") for name, size in
            sorted(by_library.items(), key=lambda item: (-item[1], item[0]))]
    lines.append("code by library or directory:")
    lines += format_table(rows, ("library", "code"))
    lines.append("")
    biggest = sorted((piece for piece in code_pieces if piece.section != FILL),
                     key=lambda piece: (-piece.size, piece.address))[:top]
    rows = [(symbol_name(piece), f"{piece.size:,}", attribute(piece, nm_files))
            for piece in biggest]
    lines.append(f"largest functions and tables in the code window (top {top}):")
    lines += format_table(rows, ("symbol", "bytes", "object"))
    lines.append("")
    biggest = sorted((piece for piece in ram_pieces if piece.section != FILL),
                     key=lambda piece: (-piece.size, piece.address))[:top]
    rows = [(symbol_name(piece), f"{piece.size:,}", attribute(piece, nm_files))
            for piece in biggest]
    lines.append(f"largest .bss and .noinit objects (top {top}):")
    lines += format_table(rows, ("symbol", "bytes", "object"))
    for name, module_map in modules:
        lines.append("")
        lines += module_report(name, module_map)[:-1]
    return "\n".join(lines)


def run_nm(elf, nm="arm-none-eabi-nm"):
    """`nm -S -l` output for the ELF, or "" when nm isn't available."""
    if elf is None or not elf.exists() or shutil.which(nm) is None:
        return ""
    result = subprocess.run([nm, "-S", "-l", str(elf)], capture_output=True, text=True,
                            check=False)
    return result.stdout if result.returncode == 0 else ""


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        description="Report what fills the device code window from the linker maps")
    parser.add_argument("--build", type=Path,
                        help="a build directory such as build/release (reads its device ELF "
                             "and map and the overlay maps)")
    parser.add_argument("--map", type=Path, help="the device map file (instead of --build)")
    parser.add_argument("--elf", type=Path, help="the device ELF, for attributing LTO code")
    parser.add_argument("--overlay-map", type=Path, help="the rescan overlay map file")
    parser.add_argument("--usb-overlay-map", type=Path, help="the USB overlay map file")
    parser.add_argument("--language-pack", type=Path, action="append", default=[],
                        help="a language pack built into the image")
    parser.add_argument("--module-map", action="append", default=[], metavar="NAME=MAP",
                        help="a module's name and map file, in the order of the module numbers")
    parser.add_argument("--nm", default="arm-none-eabi-nm", help="the nm to run on the ELF")
    parser.add_argument("--top", type=int, default=25, help="how many symbols to list")
    parser.add_argument("--objects", type=int, default=40, help="how many objects to list")
    parser.add_argument("--summary", action="store_true", help="only the lines against the limits")
    parser.add_argument("--output", type=Path, help="also save the report as UTF-8 text")
    parser.add_argument("--preset", choices=BUDGET_PRESETS,
                        help="with --build and --summary, print the budget table of tools/firmware.py sizes")
    arguments = parser.parse_args(argv)
    if arguments.build is not None:
        directory = arguments.build / "platform" / "device"
        arguments.map = arguments.map or directory / "echo_device.map"
        arguments.elf = arguments.elf or directory / "echo_device.elf"
        overlay = directory / "echo_rescan_overlay.map"
        if arguments.overlay_map is None and overlay.exists():
            arguments.overlay_map = overlay
        usb_overlay = directory / "echo_usb_overlay.map"
        if arguments.usb_overlay_map is None and usb_overlay.exists():
            arguments.usb_overlay_map = usb_overlay
    if arguments.map is None:
        parser.error("give --build or --map")
    if arguments.preset is not None and (arguments.build is None or not arguments.summary):
        parser.error("--preset needs --build and --summary")
    return arguments


def read_map(path):
    return None if path is None else path.read_text(encoding="utf-8", errors="replace")


def read_modules(arguments):
    modules = []
    for item in arguments:
        name, _, path = item.partition("=")
        modules.append((name, read_map(Path(path))))
    return modules


def main(argv=None):
    arguments = parse_arguments(argv)
    device_map = read_map(arguments.map)
    overlay_map = read_map(arguments.overlay_map)
    usb_overlay_map = read_map(arguments.usb_overlay_map)
    split = split_state(device_map, usb_overlay_map) if usb_overlay_map is not None else []
    if split:
        print("error: the main program and the USB overlay each have their own variables of "
              + ", ".join(split), file=sys.stderr)
        return 1
    packs = [(path.stem, path.stat().st_size) for path in arguments.language_pack]
    modules = read_modules(arguments.module_map)
    problems = module_problems(device_map, modules)
    if arguments.summary and arguments.preset is not None:
        try:
            text = "\n".join(budget_table(budget_rows(arguments.build, arguments.preset)))
        except ValueError as error:
            print("error: " + str(error), file=sys.stderr)
            return 1
    elif arguments.summary:
        text = "\n".join(summary(device_map, overlay_map, usb_overlay_map, packs, modules))
    else:
        nm_text = run_nm(arguments.elf, arguments.nm)
        text = report(device_map, overlay_map, nm_text, arguments.top, arguments.objects,
                      usb_overlay_map, packs, modules)
    print(text)
    if arguments.output is not None:
        arguments.output.write_text(text + "\n", encoding="utf-8")
    for problem in problems:
        print("error: " + problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
