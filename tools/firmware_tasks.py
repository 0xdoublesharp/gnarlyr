import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "firmware/tools"))
import size_report


def generators(source, python, check=False):
    style = ["--style", source / ".clang-format"]
    specifications = [
        ("lang_pack", ["--sources", source / "src/core", *style]),
        ("lang_pack", ["--export-catalogue", source.parent / "site/language-catalog.js"]),
        ("font_build", ["--config", source / "assets/fonts/fonts.json", "--output", source / "src/fonts", *style]),
        ("glyph_pack", ["--config", source / "assets/fonts/glyph_pack.json", "--output", source / "src/fonts/packs"]),
        ("fold_table", ["--output", source / "src/library", *style]),
        ("audio_resample_table", ["--output", source / "platform/device/logic/audio_resample_table.h", *style]),
        ("wma_tables", ["--output", source / "third_party/rockbox_wma/port/wma_windows.h", *style]),
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


def size_rows(build, preset):
    return size_report.budget_rows(build, preset)


def print_sizes(source):
    rows = [row for preset in size_report.BUDGET_PRESETS for row in size_rows(source / "build" / preset, preset)]
    print("\n".join(size_report.budget_table(rows)))
    if any(not row.ok for row in rows):
        raise ValueError("Firmware size floor or slot limit violated")
