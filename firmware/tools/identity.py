import json
from dataclasses import dataclass
from pathlib import Path

IDENTITY_FILE = Path(__file__).resolve().parents[1] / "identity.json"


@dataclass(frozen=True)
class Identity:
    name: str
    short_name: str
    id: str
    version: str
    data_directory: str
    update_file: str
    playlist_start_directive: str


def load(path=IDENTITY_FILE):
    return Identity(**json.loads(path.read_text(encoding="utf-8")))


IDENTITY = load()
