"""Settings backups on the player: list them, copy them to this computer, and ask for a restore.

The layouts mirror firmware/src/settings/user_state.c and docs/formats.md (Settings continuity).
The player writes its own backups when a new build first starts; this module only reads them,
copies them off the player, and leaves a restore request in the continuity mark, which the player
carries out when the USB session ends.
"""
import json
import shutil
import struct
from pathlib import Path

INTERNAL_FILES = ("settings.bin", "buttons.bin", "headphones.bin", "parametric.bin", "theme.bin",
                  "discovery.bin", "eq_slots.bin")
MUSIC_FILES = ("bluetooth.bin", "session.bin", "order.bin", "mixes.bin", "moments.bin", "listens.bin",
               "recap.bin", "track_keys.bin", "favorites.bin", "bookmarks.bin", "tags-a.bin", "tags-b.bin",
               "favorite_added.bin", "hidden.bin")
TAPES = 32
TAPE_FILES = tuple(f"mix{number:02d}{side}.bin" for number in range(TAPES) for side in "ab")
MARK_FILE = "continuity.bin"
MARK_VERSION = 1
MARK_BYTES = 24
BACKUP_FOLDER = "backup"
LIST_FILE = "list.bin"
LIST_MAGIC = b"DSBU"
LIST_VERSION = 1
LIST_BYTES = 44
BUILD_BYTES = 16
SLOTS = 2
NO_SLOT = 0xFF


def catalogue():
    """Every user-state file name with the volume it lives on: internal or music."""
    return ([(name, "internal") for name in INTERNAL_FILES] +
            [(name, "music") for name in MUSIC_FILES + TAPE_FILES])


def decode_list(data, fnv1a):
    """The backups the player lists, newest first: slot, folder and the build they came from."""
    if (len(data) < LIST_BYTES or data[:4] != LIST_MAGIC or data[4] != LIST_VERSION or
            struct.unpack_from("<I", data, LIST_BYTES - 4)[0] != fnv1a(data[:LIST_BYTES - 4])):
        return []
    builds = [data[8 + slot * BUILD_BYTES:8 + (slot + 1) * BUILD_BYTES].split(b"\0")[0].decode("ascii", "replace")
              for slot in range(SLOTS)]
    newest = data[5] if data[5] < SLOTS and builds[data[5]] else 0
    order = [(newest + step) % SLOTS for step in range(SLOTS)]
    return [{"slot": slot, "folder": f"{BACKUP_FOLDER}/{slot + 1}", "build": builds[slot],
             "newest": position == 0} for position, slot in enumerate(slot for slot in order if builds[slot])]


def mark_build(data):
    return data[8:MARK_BYTES].split(b"\0")[0]


def mark_valid(data, fnv1a):
    return (len(data) >= MARK_BYTES and data[4] == MARK_VERSION and
            struct.unpack_from("<I", data)[0] == fnv1a(mark_build(data)))


def mark_bytes(request, previous, fnv1a):
    """A continuity mark asking for a restore of slot `request - 1`, keeping the recorded build."""
    build = mark_build(previous) if mark_valid(previous, fnv1a) else b""
    return (struct.pack("<I", fnv1a(build)) + bytes([MARK_VERSION, request, 0, 0]) +
            build.ljust(BUILD_BYTES, b"\0"))


def choose(backups, choice):
    """The backup named by `newest`, `older`, a slot number (1 or 2) or a build name."""
    for position, backup in enumerate(backups):
        if choice in (("newest", "older")[position], str(backup["slot"] + 1), backup["build"]):
            return backup
    return None


class Manager:
    def __init__(self, tool, api):
        self.tool = tool
        self.api = api

    def error(self, code, message):
        raise self.api.DebugError(code, message)

    def volumes(self, music_volume=None):
        found = self.tool.locate()
        if not found["connected"]:
            self.error("player_not_connected", f"no {self.api.IDENTITY['name']} is on USB as a storage device")
        internal = Path(found["volume"])
        cards = found["usb"]["card_volumes"] if found["source"] == "usb" else []
        if len(cards) > 1:
            self.error("continuity_volume_ambiguous", "the player has several mounted card volumes")
        music = Path(music_volume) if music_volume is not None else (
            Path(cards[0]["mount_point"]) if cards else internal)
        if not (music / self.api.DATA_DIRECTORY).is_dir():
            self.error("not_a_player_volume", f"{music} has no {self.api.DATA_DIRECTORY} folder")
        return found, internal, music

    def data(self, root):
        return root / self.api.DATA_DIRECTORY

    def backups(self, internal):
        path = self.data(internal) / BACKUP_FOLDER / LIST_FILE
        return decode_list(path.read_bytes(), self.api.fnv1a) if path.is_file() else []

    def present(self, folder):
        return sorted(name for name, _ in catalogue() if (folder / name).is_file())

    def copy_files(self, sources, target):
        target.mkdir(parents=True, exist_ok=True)
        copied = []
        for name, folder in sources:
            if (folder / name).is_file():
                shutil.copyfile(folder / name, target / name)
                copied.append(name)
        return copied

    def backup(self, out=None, music_volume=None):
        """List the player's backups; with `out`, copy them and the current files to this computer."""
        _, internal, music = self.volumes(music_volume)
        backups = self.backups(internal)
        for backup in backups:
            backup["files"] = self.present(self.data(internal) / backup["folder"])
        result = {"ok": True, "internal": str(internal), "music": str(music), "backups": backups,
                  "restore_pending": self.pending(internal)}
        if out is None:
            return result
        out = Path(out).resolve()
        for root in {internal.resolve(), music.resolve()}:
            if out == root or root in out.parents:
                self.error("write_refused", "the copies do not go onto the player's drives")
        folders = {"internal": self.data(internal), "music": self.data(music)}
        current = self.copy_files([(name, folders[volume]) for name, volume in catalogue()], out / "current")
        for backup in backups:
            folder = self.data(internal) / backup["folder"]
            backup["copied"] = self.copy_files([(name, folder) for name, _ in catalogue()],
                                               out / f"backup-{backup['slot'] + 1}")
        (out / "manifest.json").write_text(json.dumps({"current": current, "backups": backups}, indent=2) + "\n")
        result.update(out=str(out), current=current)
        return result

    def pending(self, internal):
        path = self.data(internal) / MARK_FILE
        data = path.read_bytes() if path.is_file() else b""
        valid = mark_valid(data, self.api.fnv1a)
        return data[5] if valid and 0 < data[5] <= SLOTS else None

    def restore(self, choice, music_volume=None):
        """Ask the player to restore a backup when this USB session ends, then eject."""
        found, internal, _ = self.volumes(music_volume)
        backups = self.backups(internal)
        chosen = choose(backups, choice)
        if chosen is None:
            self.error("backup_missing", f"the player has no backup {choice!r}; it has "
                                         f"{[backup['build'] for backup in backups] or 'none'}")
        path = self.data(internal) / MARK_FILE
        previous = path.read_bytes() if path.is_file() else b""
        target = self.tool.write(internal, path, mark_bytes(chosen["slot"] + 1, previous, self.api.fnv1a))
        return {"ok": True, "mark_file": str(target), "restore": chosen, "ejected": self.tool.eject(found),
                "note": "the player restores the backup as the USB session ends and the app starts again"}
