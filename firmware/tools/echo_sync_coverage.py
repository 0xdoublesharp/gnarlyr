import argparse
import importlib
import json
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
MODULES = ("echo_sync", "library_index", "cover_export", "cue_sheet", "replay_gain", "fnv",
           "identity", "tag_skim", "mp4_chapters", "dsd_metadata")
PATTERNS = ("test_echo_sync*.py", "test_library_index.py", "test_cover_export.py",
            "test_cue_sheet.py", "test_replay_gain.py", "test_tag_skim.py", "test_mp4_chapters.py")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Measure and enforce echo-sync toolchain coverage")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--measure", action="store_true")
    args = parser.parse_args(argv)
    output = args.output.resolve()
    if output.is_relative_to(TOOLS.parents[1]):
        parser.error("coverage output must be outside the source checkout")
    try:
        import coverage
    except ImportError:
        parser.error("install tools/requirements-test.txt in the tools Python environment first")
    output.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(TOOLS))
    measured = coverage.Coverage(branch=True, config_file=False,
                                 data_file=str(output / "coverage.data"),
                                 include=[str(TOOLS / f"{name}.py") for name in MODULES])
    measured.start()
    result = None
    try:
        for name in MODULES:
            importlib.import_module(name)
        suite = unittest.TestSuite()
        loader = unittest.TestLoader()
        for pattern in PATTERNS:
            suite.addTests(loader.discover(str(TOOLS / "tests"), pattern=pattern))
        result = unittest.TextTestRunner(verbosity=1).run(suite)
    finally:
        measured.stop()
        measured.save()
    measured.json_report(outfile=str(output / "coverage.json"))
    measured.html_report(directory=str(output / "html"))
    measured.report(show_missing=True)
    data = json.loads((output / "coverage.json").read_text())
    gaps = []
    for name in MODULES:
        module = next((value for path, value in data["files"].items()
                       if Path(path).resolve() == TOOLS / f"{name}.py"), None)
        if module is None or module["missing_lines"] or module["missing_branches"]:
            gaps.append(name)
    if not args.measure and gaps:
        print("Coverage below 100% lines/branches: " + ", ".join(gaps), file=sys.stderr)
    return 0 if result.wasSuccessful() and (args.measure or not gaps) else 1


if __name__ == "__main__":
    raise SystemExit(main())
