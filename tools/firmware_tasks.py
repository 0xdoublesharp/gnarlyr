import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


def generators(source, python, check=False):
    style = ["--style", source / ".clang-format"]
    specifications = [
        ("lang_pack", ["--sources", source / "src/core", *style]),
        ("lang_pack", ["--export-catalogue", source.parent / "site/language-catalog.js"]),
        ("font_build", ["--config", source / "assets/fonts/fonts.json", "--output", source / "src/fonts", *style]),
        ("glyph_pack", ["--config", source / "assets/fonts/glyph_pack.json", "--output", source / "src/fonts/packs"]),
        ("fold_table", ["--output", source / "src/library", *style]),
        ("audio_resample_table", ["--output", source / "platform/device/logic/audio_resample_table.h", *style]),
        ("replay_gain_table", ["--output", source / "src/library/replay_gain_table.h", *style]),
    ]
    return [[str(python), str(source / "tools" / f"{name}.py"), *map(str, arguments),
             *(["--check"] if check else [])] for name, arguments in specifications]


def regenerate(source, env, python, check=False):
    failures = []
    for command in generators(source, python, check):
        print("+", " ".join(command), flush=True)
        if subprocess.run(command, cwd=source, env=env, check=False).returncode:
            failures.append(Path(command[1]).name)
    if failures:
        raise ValueError(f"Generators failed: {', '.join(failures)}; run python3 tools/firmware.py regen")


@dataclass(frozen=True)
class Size:
    preset: str
    name: str
    used: int
    limit: int
    floor: int = 0

    @property
    def ok(self):
        return self.limit - self.used >= self.floor


def size_rows(build, preset):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "firmware/tools"))
    import size_report as report
    import make_image

    directory = build / "platform/device"
    device = (directory / "echo_device.map").read_text()
    regions = report.memory_regions(device)
    sizes = report.output_sizes(device)
    symbols = report.linker_symbols(device)
    origin, capacity = regions[report.DATA_REGION]
    rows = [Size(preset, "resident code", sizes[".code"], regions[report.CODE_REGION][1],
                 4096 if preset == "release" else 1024),
            Size(preset, "static RAM", symbols[report.DATA_END_SYMBOL] - origin,
                 symbols.get(report.DATA_LIMIT_SYMBOL, origin + capacity) - origin),
            Size(preset, "decoder workspace", sizes[".high_ram"], regions[report.WORKSPACE_REGION][1])]
    modules = []
    for path in sorted(directory.glob("*.map")):
        text = path.read_text()
        memory = report.memory_regions(text)
        sections = report.output_sizes(text)
        if path.stem.startswith("echo_module_"):
            name = path.stem.removeprefix("echo_module_")
            modules.append((name, text))
            rows.append(Size(preset, f"module {name}", sum(report.module_extent(text)), memory[report.MODULE_REGION][1]))
        elif path.stem in ("echo_rescan_overlay", "echo_usb_overlay"):
            rows.append(Size(preset, path.stem.removeprefix("echo_"), sum(report.module_extent(text)), memory[report.OVERLAY_REGION][1]))
            if report.USB_IMAGE_REGION in memory:
                rows.append(Size(preset, "USB image slot", sections[".overlay"], memory[report.USB_IMAGE_REGION][1]))
        elif path.stem.startswith("echo_core1"):
            name = path.stem.removeprefix("echo_")
            script = Path(__file__).resolve().parents[1] / "firmware/platform/device/core1" / f"{name}.ld"
            match = re.search(r"ASSERT\(core1_bss_end <= (0x[0-9A-Fa-f]+)", script.read_text())
            if match is None:
                raise ValueError(f"Missing core 1 stack boundary: {script}")
            static_limit = int(match[1], 16) - memory["CORE1_DATA"][0]
            code_limit = {"core1_vorbis": make_image.CORE1_VORBIS_CAPACITY,
                          "core1_m4a": make_image.CORE1_M4A_CAPACITY}.get(name, make_image.CORE1_MODULE_CAPACITY)
            rows.append(Size(preset, f"{name} image bank", sections[".text"], code_limit))
            rows.append(Size(preset, f"{name} static RAM", sections.get(".bss", 0) + sections.get(".data", 0), static_limit))
    expected = {"views", "settings", "library", "listening", "dsp", "ogg", "process", "extension"}
    required = {"echo_usb_overlay.map", "echo_rescan_overlay.map", "echo_core1.map", "echo_core1_mp3.map",
                "echo_core1_m4a.map", "echo_core1_vorbis.map", "echo_core1_opus.map"}
    if {name for name, _ in modules} != expected or not required <= {path.name for path in directory.glob("*.map")}:
        raise ValueError(f"Incomplete size inputs in {directory}; rebuild both presets")
    problems = report.module_problems(device, modules)
    for _, text in modules:
        if report.split_state(device, text):
            raise ValueError("Split module state")
    if problems:
        raise ValueError("; ".join(problems))
    packs = sorted((build / "lang").glob("*.pack"))
    if not packs:
        raise ValueError(f"Missing language packs in {build}")
    for path in packs:
        rows.append(Size(preset, f"language {path.stem}", path.stat().st_size, regions[report.LANGUAGE_REGION][1]))
    return rows


def print_sizes(source):
    from importlib import import_module

    rows = [row for preset in ("release", "device") for row in size_rows(source / "build" / preset, preset)]
    report = import_module("size_report")
    table = [(row.preset, row.name, f"{row.used:,}", f"{row.limit:,}",
              f"{row.limit - row.used:,}", f"{row.floor:,}", "PASS" if row.ok else "FAIL") for row in rows]
    print("\n".join(report.format_table(table, ("Preset", "Region / slot", "Used", "Limit", "Free", "Floor", "Result"))))
    if any(not row.ok for row in rows):
        raise ValueError("Firmware size floor or slot limit violated")
