import argparse
import re
import sys
from pathlib import Path

CHECKED = ("src/app", "src/ui", "src/core", "src/settings", "src/playback",
           "src/library/library_browse.c", "platform/device/board")
GENERATED = {"src/core/strings_english.h"}
ALLOWLIST = Path(__file__).resolve().parent / "literal_allowlist.txt"
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
ESCAPE = re.compile(r"\\x[0-9A-Fa-f]{2}|\\.")
WORD = re.compile(r"[A-Za-z]{2,}")
NOT_TEXT = (
    re.compile(r"^[ \t]*#[ \t]*include[^\n]*$", re.MULTILINE),
    re.compile(r"_Static_assert\s*\(.*?\)\s*;", re.DOTALL),
    re.compile(r"__asm__\s*(?:volatile\s*)?\(.*?\)\s*;", re.DOTALL),
    re.compile(r"__attribute__\s*\(\(.*?\)\)", re.DOTALL),
)
ENTRY = re.compile(r'^(\S+): (".*")$')


def text_literals(source):
    """The string literals of a C source that hold a word of two or more letters."""
    for pattern in NOT_TEXT:
        source = pattern.sub("", source)
    found = set()
    for match in LITERAL.finditer(source):
        if WORD.search(ESCAPE.sub(" ", match.group(1))):
            found.add(match.group(0))
    return found


def checked_files(root):
    for checked in CHECKED:
        place = root / checked
        for path in [place] if place.is_file() else sorted(place.rglob("*.[ch]")):
            name = path.relative_to(root).as_posix()
            if name not in GENERATED:
                yield name, path


def literals_in(root):
    """{(file, literal)} over the checked directories."""
    return {(name, literal)
            for name, path in checked_files(root)
            for literal in text_literals(path.read_text(encoding="utf-8"))}


def read_allowlist(path):
    allowed = set()
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip() or line.startswith("#"):
            continue
        match = ENTRY.match(line)
        if not match:
            raise ValueError(f"{path.name}:{number}: expected file: \"literal\"")
        allowed.add((match.group(1), match.group(2)))
    return allowed


def compare(found, allowed):
    """(literals without an entry, entries without a literal), each sorted."""
    return sorted(found - allowed), sorted(allowed - found)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Fail when screen code holds text that is not in the language pack")
    parser.add_argument("root", type=Path)
    parser.add_argument("--allowlist", type=Path, default=ALLOWLIST)
    arguments = parser.parse_args(argv)
    found = literals_in(arguments.root.resolve())
    added, stale = compare(found, read_allowlist(arguments.allowlist))
    for name, literal in added:
        print(f"{name}: {literal} is text in code: give it a name in assets/lang/en.txt and use "
              f"i18n_text, or list it in {arguments.allowlist.name} if no user reads it")
    for name, literal in stale:
        print(f"{arguments.allowlist.name}: {name}: {literal} is no longer in the code")
    print(f"{len(found)} literals with words, {len(added)} not listed, {len(stale)} stale")
    return 1 if added or stale else 0


if __name__ == "__main__":
    sys.exit(main())
