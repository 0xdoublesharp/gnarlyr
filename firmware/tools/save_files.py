import argparse
from pathlib import Path

from identity import IDENTITY

SAVE_FILES = ("settings.bin", "session.bin", "favorites.bin")
SAVE_FILE_SIZE = 4096
LIBRARY_FILE = "library.db"
LIBRARY_FILE_SIZE = 4 * 1024 * 1024


def prepare_file(path, size):
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < size:
        path.write_bytes(data + bytes(size - len(data)))


def prepare(folder):
    folder.mkdir(parents=True, exist_ok=True)
    for name in SAVE_FILES:
        prepare_file(folder / name, SAVE_FILE_SIZE)
    prepare_file(folder / LIBRARY_FILE, LIBRARY_FILE_SIZE)


def main():
    parser = argparse.ArgumentParser(
        description="Create the fixed-size files the player overwrites to save its state"
    )
    parser.add_argument("--folder", type=Path, required=True,
                        help=f"the card's /{IDENTITY.data_directory} folder")
    arguments = parser.parse_args()
    prepare(arguments.folder)


if __name__ == "__main__":
    main()
