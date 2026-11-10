"""Lists what the modules call in the main program, as linker options for the main link.

A module is linked against the finished main program, so every function and table it uses
there must survive link-time optimisation under its own name. The names are the symbols the
module's objects leave undefined; each becomes a `--require-defined` option in a response file
the main link reads.
"""
import argparse
import subprocess
import sys
from pathlib import Path

RUNTIME_PREFIX = "__"
UNDEFINED = "Uw"
OPTION = "--require-defined="


class ModuleImportError(Exception):
    pass


def parse_nm(text):
    """(defined, undefined) global symbol names of one `nm -g` listing."""
    defined, undefined = set(), set()
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 2 or fields[0].endswith(":"):
            continue
        kind, name = fields[-2], fields[-1]
        (undefined if kind in UNDEFINED else defined).add(name)
    return defined, undefined - defined


def module_symbols(listings):
    """(defined, undefined) of a module: what its objects hold and what they leave to others."""
    defined, undefined = set(), set()
    for text in listings:
        holds, needs = parse_nm(text)
        defined |= holds
        undefined |= needs
    return defined, undefined - defined


def imports(modules):
    """The names every module needs from the main program, sorted.

    `modules` maps a module's name to the `nm -g` listings of its objects. A name another
    module defines is refused: one module is in memory at a time.
    """
    symbols = {name: module_symbols(listings) for name, listings in modules.items()}
    needed = set()
    for name, (_, undefined) in symbols.items():
        for other, (defined, _) in symbols.items():
            shared = sorted(undefined & defined) if other != name else []
            if shared:
                raise ModuleImportError(f"module {name} uses {', '.join(shared)} of module {other}: "
                                   "put what both need into the main program or into one module")
        needed |= {symbol for symbol in undefined if not symbol.startswith(RUNTIME_PREFIX)}
    return sorted(needed)


def response_file(names):
    return "".join(OPTION + name + "\n" for name in names)


def run_nm(nm, path):
    return subprocess.run([nm, "-g", str(path)], capture_output=True, text=True, check=True).stdout


def write_if_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        description="Write the linker options that keep what the modules use in the main program")
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("--module", action="append", nargs="+", default=[],
                        metavar=("NAME", "OBJECT"), help="a module's name and its object files")
    parser.add_argument("--output", type=Path, required=True, help="the response file to write")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        modules = {name: [run_nm(arguments.nm, path) for path in objects]
                   for name, *objects in arguments.module}
        names = imports(modules)
    except (ModuleImportError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    write_if_changed(arguments.output, response_file(names))
    print(f"{len(names)} names the modules use in the main program -> {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
