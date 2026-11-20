import json
from dataclasses import dataclass
from pathlib import Path

IDENTITY_FILE = Path(__file__).resolve().parents[1] / "identity.json"


@dataclass(frozen=True)
class Identity:
    name: str
    short_name: str
    manufacturer: str
    id: str
    version: str
    data_directory: str
    update_file: str
    playlist_start_directive: str
    former_ids: tuple


def parse(text):
    fields = json.loads(text)
    if not isinstance(fields, dict):
        raise ValueError("identity.json does not hold an object")
    return fields


def read(path=IDENTITY_FILE):
    return parse(path.read_text(encoding="utf-8"))


def load(path=IDENTITY_FILE):
    fields = read(path)
    return Identity(**{**fields, "former_ids": tuple(fields["former_ids"])})


IDENTITY = load()
