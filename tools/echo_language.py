import importlib.util
import json
import math
import os
import re
import stat
import subprocess
from contextlib import contextmanager
from functools import lru_cache
from pathlib import Path

BUILD = re.compile(r"[0-9a-fA-F]{7,40}")
CODE = re.compile(r"[A-Za-z0-9-]{1,5}")
PACK_LIMIT = 16384
DIRECTORY_LIMIT = 256


@lru_cache(maxsize=1)
def pack_tools(root):
    spec = importlib.util.spec_from_file_location("echo_language_format", root / "firmware/tools/lang_pack.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def git(root, *arguments):
    result = subprocess.run(["git", *arguments], cwd=root, capture_output=True, check=False)
    if result.returncode:
        raise ValueError("the installed build cannot be resolved in this repository")
    return result.stdout.decode("utf-8")


def constant(source, name):
    match = re.search(r"\b" + name + r"\s*=\s*(\d+)\b", source)
    if match is None:
        raise ValueError("the installed settings layout is unknown")
    return int(match.group(1))


class Snapshot:
    def __init__(self, api, build):
        self.api = api
        if not isinstance(build, str) or not BUILD.fullmatch(build):
            code = "language_build_dirty" if isinstance(build, str) and "+" in build else "language_build_unknown"
            raise api.DebugError(code, "language operations require an exact clean installed firmware build")
        self.format = pack_tools(api.ROOT)
        try:
            self.commit = git(api.ROOT, "rev-parse", "--verify", build + "^{commit}").strip()
            self.build = build
            self.catalogue = self.format.read_catalogue_text(self.show("firmware/assets/lang/en.txt"))
            self.drawable = self.format.drawable_from_config(
                json.loads(self.show("firmware/assets/fonts/fonts.json")),
                json.loads(self.show("firmware/assets/fonts/glyph_pack.json")))
            identity = json.loads(self.show("firmware/identity.json"))
            if identity["data_directory"] != api.DATA_DIRECTORY or identity["name"] != api.IDENTITY["name"]:
                raise ValueError("the installed firmware identity differs from this tool")
            source = self.show("firmware/src/settings/settings.c")
            self.settings_version = constant(source, "VERSION")
            if (constant(source, "LANGUAGE_VERSION"), constant(source, "LANGUAGE_FIELD"),
                    constant(source, "HALF_DB_CHECKSUM_FIELD")) != (12, 19, 38):
                raise ValueError("the installed settings layout is unsupported")
        except (ValueError, KeyError, OSError, self.format.PackError) as error:
            raise api.DebugError("language_build_unknown", str(error)) from error

    def show(self, path):
        return git(self.api.ROOT, "show", self.commit + ":" + path)

    def validate(self, data):
        try:
            return self.format.validate_pack(data, self.catalogue, self.drawable)
        except self.format.PackError as error:
            raise self.api.DebugError("language_pack_invalid", str(error)) from error

    def english(self):
        return self.validate(self.format.build_pack("en", self.catalogue, {}))

    def metadata(self):
        return {"firmware_build": self.build, "firmware_commit": self.commit,
                "catalogue_hash": self.format.catalogue_hash(self.catalogue),
                "catalogue_count": len(self.catalogue)}


@contextmanager
def directory(root, parts=(), create=False):
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    descriptors = []
    try:
        descriptor = os.open(root, flags)
        descriptors.append(descriptor)
        for part in parts:
            if part in ("", ".", "..") or "/" in part or "\\" in part:
                raise OSError("unsafe language directory component")
            if create:
                try:
                    os.mkdir(part, dir_fd=descriptor)
                except FileExistsError:
                    pass
            descriptor = os.open(part, flags, dir_fd=descriptor)
            descriptors.append(descriptor)
        yield descriptor
    finally:
        for descriptor in reversed(descriptors):
            os.close(descriptor)


def read_at(descriptor, name, limit):
    file = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=descriptor)
    try:
        info = os.fstat(file)
        if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
            raise OSError("language input must be a bounded regular file")
        data = bytearray()
        while len(data) <= limit:
            chunk = os.read(file, limit + 1 - len(data))
            if not chunk:
                break
            data.extend(chunk)
        if len(data) > limit or len(data) != info.st_size:
            raise OSError("language input changed while being read")
        return bytes(data)
    finally:
        os.close(file)


def read_file(root, parts, limit):
    with directory(root, parts[:-1]) as descriptor:
        return read_at(descriptor, parts[-1], limit)


def write_stage(descriptor, name, data):
    file = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=descriptor)
    try:
        offset = 0
        while offset < len(data):
            written = os.write(file, data[offset:])
            if written <= 0:
                raise OSError("language stage could not be completely written")
            offset += written
        os.fsync(file)
    except BaseException:
        os.unlink(name, dir_fd=descriptor)
        raise
    finally:
        os.close(file)
    try:
        if read_at(descriptor, name, PACK_LIMIT) != data:
            raise OSError("language stage failed readback")
    except BaseException:
        os.unlink(name, dir_fd=descriptor)
        raise


def publish(root, data_directory, code, data, snapshot):
    with directory(root, (data_directory, "lang"), create=True) as descriptor:
        name = code + ".pack"
        stage = name + ".part"
        old = None
        try:
            old = read_at(descriptor, name, PACK_LIMIT)
        except FileNotFoundError:
            pass
        write_stage(descriptor, stage, data)
        backup_stage_owned = False
        try:
            snapshot.validate(read_at(descriptor, stage, PACK_LIMIT))
            if old is not None:
                backup = name + ".bak"
                backup_stage = backup + ".part"
                try:
                    info = os.stat(backup, dir_fd=descriptor, follow_symlinks=False)
                    if not stat.S_ISREG(info.st_mode):
                        raise OSError("the language backup target is not a regular file")
                except FileNotFoundError:
                    pass
                write_stage(descriptor, backup_stage, old)
                backup_stage_owned = True
                os.replace(backup_stage, backup, src_dir_fd=descriptor, dst_dir_fd=descriptor)
            os.replace(stage, name, src_dir_fd=descriptor, dst_dir_fd=descriptor)
            if read_at(descriptor, name, PACK_LIMIT) != data:
                raise OSError("published language pack failed readback; backup retained")
            os.fsync(descriptor)
        finally:
            for owned in ([stage, backup_stage] if backup_stage_owned else [stage]):
                try:
                    os.unlink(owned, dir_fd=descriptor)
                except FileNotFoundError:
                    pass
    return {"pack_file": str(root / data_directory / "lang" / name),
            "backup_file": str(root / data_directory / "lang" / backup) if old is not None else None}


class Manager:
    def __init__(self, tool, api):
        self.tool = tool
        self.api = api

    def error(self, code, message):
        raise self.api.DebugError(code, message)

    def path_argument(self, value):
        if not isinstance(value, (str, os.PathLike)):
            self.error("usage", "a path must be a string")
        return Path(value)

    def code(self, value):
        if not isinstance(value, str) or not CODE.fullmatch(value):
            self.error("language_code_invalid", "a language code has one to five letters, digits or '-' characters")
        return value

    def context(self, music_volume=None):
        found = self.tool.discover()
        if not found["connected"]:
            self.error("player_not_connected", "language management requires the connected USB player")
        internal = Path(found["volume"])
        if self.tool.volume is not None and self.tool.volume.resolve() != internal.resolve():
            self.error("language_volume_refused", "the volume override is not this USB player's internal drive")
        cards = found["usb"]["card_volumes"]
        players = self.api.find_players(self.tool.host.usb_tree())
        if len(players) != 1 or players[0].get("serial_num") != found["usb"].get("serial"):
            self.error("language_player_changed", "the identified USB player changed during discovery")
        card_media = [item for item in players[0].get("Media", [])
                      if self.api.CARD_MEDIUM.lower() in item.get("_name", "").lower()]
        if card_media and not cards:
            self.error("language_volume_unmounted", "the player's music card is not mounted")
        if len(cards) > 1 or len(card_media) > 1:
            self.error("language_volume_ambiguous", "the player has several mounted card volumes")
        music = Path(cards[0]["mount_point"]) if cards else internal
        if music_volume is not None and self.path_argument(music_volume).resolve() != music.resolve():
            self.error("language_volume_refused", "the requested volume is not the active music drive of this USB player")
        reports = []
        for name in self.api.REPORT_FILES:
            try:
                data = read_file(internal, (self.api.DATA_DIRECTORY, self.api.DEBUG_DIRECTORY, name),
                                 self.api.REPORT_BYTES)
            except FileNotFoundError:
                continue
            report = self.api.parse_report(data.decode("utf-8"))
            if report.get("truncated"):
                self.error("bad_report", "a truncated report cannot bind a language write")
            reports.append(report)
        if not reports:
            self.error("no_report", "language management requires the installed firmware report")
        report = max(reports, key=lambda item: item["values"].get("report.sequence", -1))
        build = report["values"].get("firmware.build")
        if report["values"].get("firmware.name") != self.api.IDENTITY["name"]:
            self.error("language_player_unidentified", "the installed report has a different firmware identity")
        if not isinstance(found["usb"].get("serial"), str) or not found["usb"]["serial"]:
            self.error("language_player_unidentified", "the USB player has no stable serial identity")
        snapshot = Snapshot(self.api, build)
        return {"found": found, "internal": internal, "music": music, "snapshot": snapshot,
                "serial": found["usb"].get("serial")}

    def recheck(self, context):
        newer = self.context(context["music"])
        if any(newer[key] != context[key] for key in ("internal", "music", "serial")) or \
                newer["snapshot"].commit != context["snapshot"].commit:
            self.error("language_player_changed", "the player, active music drive or installed build changed")
        return newer

    def local_pack(self, path, snapshot):
        path = self.path_argument(path)
        return read_file(path.parent, (path.name,), PACK_LIMIT)

    def inspect(self, path, build=None):
        snapshot = Snapshot(self.api, build) if build is not None else self.context()["snapshot"]
        return {"ok": True, **snapshot.metadata(), "pack": snapshot.validate(self.local_pack(path, snapshot))}

    def selected(self, context):
        for root in dict.fromkeys((context["internal"], context["music"])):
            try:
                data = read_file(root, (self.api.DATA_DIRECTORY, "settings.bin"), 4096)
            except FileNotFoundError:
                continue
            version = int.from_bytes(data[4:6], "little")
            if (len(data) < 42 or data[:4] != b"DSST" or
                    not 12 <= version <= context["snapshot"].settings_version or
                    context["snapshot"].format.fnv(data[:38]) != int.from_bytes(data[38:42], "little")):
                self.error("language_settings_invalid", "the saved language setting cannot be verified")
            raw = data[19:24]
            code, separator, tail = raw.partition(b"\0")
            if separator and any(tail):
                self.error("language_settings_invalid", "the saved language code has nonzero padding")
            try:
                return self.code(code.decode("ascii")) if code else "en"
            except UnicodeError:
                self.error("language_settings_invalid", "the saved language code is not ASCII")
        return "en"

    def packs(self, context):
        snapshot = context["snapshot"]
        entries = [{**snapshot.english(), "source": "builtin", "path": None}]
        try:
            with directory(context["music"], (self.api.DATA_DIRECTORY, "lang")) as descriptor:
                names = sorted(os.listdir(descriptor))
                if len(names) > DIRECTORY_LIMIT:
                    self.error("language_directory_large", "the language folder has too many entries")
                for name in names:
                    if name.startswith(".") or not name.endswith(".pack"):
                        continue
                    code = self.code(name[:-5])
                    if code.lower() == "en":
                        self.error("language_english_protected", "an external English pack would hide the builtin fallback")
                    info = snapshot.validate(read_at(descriptor, name, PACK_LIMIT))
                    if info["code"] != code:
                        self.error("language_code_mismatch", "the pack filename differs from its language code")
                    entries.append({**info, "source": "file", "path": str(context["music"] / self.api.DATA_DIRECTORY / "lang" / name)})
        except FileNotFoundError:
            pass
        if (len(entries) > 8 or len({item["name"] for item in entries}) != len(entries) or
                len({item["code"].lower() for item in entries}) != len(entries)):
            self.error("language_choices_ambiguous", "the language menu is full or has duplicate native names")
        return entries

    def list(self, music_volume=None):
        context = self.context(music_volume)
        selected = self.selected(context)
        languages = [{key: value for key, value in entry.items() if key != "texts"}
                     for entry in self.packs(context)]
        saved = selected
        selected = selected if selected in {entry["code"] for entry in languages} else "en"
        for entry in languages:
            entry["selected"] = entry["code"] == selected
        return {"ok": True, **context["snapshot"].metadata(), "music_volume": str(context["music"]),
                "selected": selected, "saved_language": saved, "languages": languages}

    def safe_script_text(self, text, label=False):
        if (not isinstance(text, str) or not text or text != text.strip() or
                not text.isprintable() or "#" in text or (label and "=" in text)):
            self.error("language_script_unsafe", "the localized language menu text is unsafe for a debug script")
        return text

    def timeout(self, value):
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
            self.error("usage", "timeout must be a positive finite number")
        return value

    def selection_text(self, context, code, entries=None):
        entries = entries if entries is not None else {entry["code"]: entry for entry in self.packs(context)}
        active = self.selected(context)
        active = active if active in entries else "en"
        text = "build " + context["snapshot"].build + "\n"
        if code is not None:
            self.code(code)
            if code not in entries:
                self.error("language_not_installed", "the requested language is not installed")
            texts = entries[active]["texts"]
            key = "STR_SETTING_LANGUAGE" if "STR_SETTING_LANGUAGE" in texts else "STR_LANGUAGE"
            label = self.safe_script_text(texts[key], label=True)
            name = self.safe_script_text(entries[code]["name"])
            text += "set " + label + "=" + name + "\nwait 250\n"
        return self.api.prepare_script(text + "reconnect\n", context["snapshot"].build).decode("utf-8")

    def refresh(self, context, code, timeout):
        text = self.selection_text(context, code)
        self.recheck(context)
        runner = self.api.Tool(host=self.tool.host, volume_name=self.tool.volume_name,
                               sleep=self.tool.sleep, clock=self.tool.clock)
        result = runner.run_text(text, self.timeout(timeout))
        newer = self.recheck(context)
        actual = self.selected(newer)
        if code is not None and actual != code:
            self.error("language_not_selected", "the returned settings do not confirm the requested language")
        return {"run": result, "selected": actual}

    def install(self, path, select=False, music_volume=None, timeout=420.0):
        if type(select) is not bool:
            self.error("usage", "select must be a boolean")
        self.timeout(timeout)
        context = self.context(music_volume)
        data = self.local_pack(path, context["snapshot"])
        info = context["snapshot"].validate(data)
        code = self.code(info["code"])
        if code.lower() == "en":
            self.error("language_english_protected", "builtin English cannot be replaced")
        existing = self.packs(context)
        if any(item["code"].lower() == code.lower() and item["code"] != code for item in existing):
            self.error("language_code_mismatch", "the language code conflicts with an existing FAT filename")
        if any(item["code"] != code and item["name"] == info["name"] for item in existing):
            self.error("language_choices_ambiguous", "another language already uses this native name")
        if code not in {item["code"] for item in existing} and len(existing) >= 8:
            self.error("language_choices_ambiguous", "the firmware language menu is full")
        choices = {item["code"]: item for item in existing}
        choices[code] = info
        self.selection_text(context, code if select else None, choices)
        self.recheck(context)
        published = publish(context["music"], self.api.DATA_DIRECTORY, code, data, context["snapshot"])
        refreshed = self.refresh(context, code if select else None, timeout)
        return {"ok": True, **context["snapshot"].metadata(), **published, **refreshed,
                "music_volume": str(context["music"]), "pack": {key: value for key, value in info.items() if key != "texts"}}

    def select(self, code, music_volume=None, timeout=420.0):
        self.code(code)
        self.timeout(timeout)
        context = self.context(music_volume)
        return {"ok": True, **context["snapshot"].metadata(),
                "music_volume": str(context["music"]), **self.refresh(context, code, timeout)}
