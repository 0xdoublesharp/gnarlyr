import argparse
import os
import sys
from pathlib import Path

from identity import IDENTITY, IDENTITY_FILE

SKIPPED_DIRECTORIES = {"build", "docs", "external", "third_party"}
GENERATED_DIRECTORIES = {("tests", "fixtures")}
SKIPPED_SUFFIXES = {".md"}
SKIPPED_NAMES = {"LICENSE", IDENTITY_FILE.name}


def forbidden_words(identity=IDENTITY):
    names = [identity.name, identity.short_name, identity.manufacturer, identity.id,
             identity.data_directory.lstrip("."), Path(identity.update_file).stem,
             identity.playlist_start_directive, *identity.former_ids]
    return sorted({name.casefold() for name in names})


def is_skipped_directory(parts):
    return (parts[-1] in SKIPPED_DIRECTORIES or parts[-1].startswith(".")
            or any(parts[-len(generated):] == generated for generated in GENERATED_DIRECTORIES))


def source_files(root):
    for folder, directories, files in os.walk(root):
        parts = Path(folder).relative_to(root).parts
        directories[:] = sorted(name for name in directories if not is_skipped_directory((*parts, name)))
        for name in sorted(files):
            path = Path(folder, name)
            if name not in SKIPPED_NAMES and path.suffix not in SKIPPED_SUFFIXES:
                yield path


def text_of(path):
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return None


def find_violations(root, words):
    violations = []
    for path in source_files(root):
        text = text_of(path)
        for number, line in enumerate((text or "").splitlines(), start=1):
            folded = line.casefold()
            for word in words:
                if word in folded:
                    violations.append(f"{path.relative_to(root)}:{number}: {word}")
    return violations


def main():
    parser = argparse.ArgumentParser(
        description="Fail when a source file spells one of the firmware's names, present or former, "
                    "which belong in identity.json alone")
    parser.add_argument("roots", type=Path, nargs="+")
    words = forbidden_words()
    violations = []
    for root in parser.parse_args().roots:
        violations += [f"{root.name}/{violation}" for violation in find_violations(root.resolve(), words)]
    for violation in violations:
        print(f"name found: {violation}")
    print(f"{len(violations)} name(s) outside identity.json; looked for {', '.join(words)}")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
