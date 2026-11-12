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
PACK_LIMIT = 18432
FLAG_MASKS = (0xF8, 0xF0, 0xC8, 0xC0)
USB_ANSWER_BITS = 0x30
DIRECTORY_LIMIT = 256
ASCII_LOWER = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")


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


HOME_FIRST_ITEMS = 9
HOME_SLOTS = 10


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
        self.flags_reserved = 0xF8
        self.status_items_supported = False
        try:
            self.commit = git(api.ROOT, "rev-parse", "--verify", build + "^{commit}").strip()
            self.build = build
            self.pack_limit = constant(self.show("firmware/src/core/include/core/i18n.h"),
                                       "I18N_PACK_CAPACITY")
            if not 0 < self.pack_limit <= PACK_LIMIT:
                raise ValueError("the installed language capacity is unsupported")
            self.catalogue = self.format.read_catalogue_text(self.show("firmware/assets/lang/en.txt"))
            self.drawable = self.format.drawable_from_config(
                json.loads(self.show("firmware/assets/fonts/fonts.json")),
                json.loads(self.show("firmware/assets/fonts/glyph_pack.json")))
            identity = json.loads(self.show("firmware/identity.json"))
            if identity["data_directory"] != api.DATA_DIRECTORY or identity["name"] != api.IDENTITY["name"]:
                raise ValueError("the installed firmware identity differs from this tool")
            source = self.show("firmware/src/settings/settings.c")
            self.settings_version = constant(source, "VERSION")
            if (self.settings_version not in (12, 13, 14, 15) or
                    (constant(source, "LANGUAGE_VERSION"), constant(source, "LANGUAGE_FIELD"),
                     constant(source, "HALF_DB_CHECKSUM_FIELD")) != (12, 19, 38)):
                raise ValueError("the installed settings layout is unsupported")
            if self.settings_version >= 14:
                self.check_replay_gain_layout(source)
            if self.settings_version >= 15 and (
                    constant(source, "EXTENSION_VERSION"), constant(source, "EXTENSION_FIELD"),
                    constant(source, "EXTENSION_CHECKSUM_FIELD")) != (15, 38, 54):
                raise ValueError("the installed settings extension layout is unsupported")
            self.clock_offset_byte = (constant(source, "CLOCK_OFFSET_BYTE")
                                      if re.search(r"\bCLOCK_OFFSET_BYTE\s*=", source) else None)
            if self.clock_offset_byte not in (None, 11):
                raise ValueError("the installed clock offset layout is unsupported")
            if self.settings_version >= 15:
                self.check_home_layout()
                self.status_items_supported = "STATUS_ITEMS_BYTE" in source
                if self.status_items_supported and constant(source, "STATUS_ITEMS_BYTE") != 13:
                    raise ValueError("the installed status bar settings layout is unsupported")
                self.check_speed_layout(source)
        except (ValueError, KeyError, OSError, self.format.PackError) as error:
            raise api.DebugError("language_build_unknown", str(error)) from error

    def check_replay_gain_layout(self, source):
        modes = self.show("firmware/src/core/include/core/replay_gain.h")
        declaration = re.search(r"typedef\s+enum\s+ReplayGainMode\s*\{([^}]+)\}", modes)
        names = [name.strip() for name in declaration.group(1).split(",")] if declaration else []
        compact = re.sub(r"\s+", "", source)
        masks = [mask for mask in FLAG_MASKS
                 if f"(flags&0x{mask:02X}u)==0&&(flags&3u)<REPLAY_GAIN_MODE_COUNT" in compact]
        if (constant(source, "REPLAY_GAIN_VERSION") != 14 or
                names != ["REPLAY_GAIN_OFF", "REPLAY_GAIN_TRACK", "REPLAY_GAIN_ALBUM",
                          "REPLAY_GAIN_MODE_COUNT"] or len(masks) != 1):
            raise ValueError("the installed ReplayGain settings layout is unsupported")
        self.reserved_flags = masks[0]

    def check_home_layout(self):
        header = self.show("firmware/src/settings/include/settings/settings.h")
        self.home_items = constant(header, "SETTINGS_HOME_ITEM_COUNT")
        self.home_settings_item = constant(header, "SETTINGS_HOME_SETTINGS_ITEM")
        if ((constant(header, "SETTINGS_HOME_FIRST_ITEM_COUNT"), constant(header, "SETTINGS_HOME_ORDER_SLOTS"))
                != (HOME_FIRST_ITEMS, HOME_SLOTS) or not HOME_FIRST_ITEMS <= self.home_items <= HOME_SLOTS or
                self.home_settings_item >= HOME_FIRST_ITEMS):
            raise ValueError("the installed Home layout is unsupported")

    def check_speed_layout(self, source):
        self.speed_steps = (0, 0)
        if re.search(r"\bSPEED_BYTE\s*=", source) is None:
            return
        header = self.show("firmware/src/settings/include/settings/settings.h")
        count, normal = constant(header, "SETTINGS_SPEED_COUNT"), constant(header, "SETTINGS_SPEED_NORMAL")
        if constant(source, "SPEED_BYTE") != 3 or not 0 <= normal < count <= 128:
            raise ValueError("the installed playback speed layout is unsupported")
        self.speed_steps = (-normal, count - 1 - normal)

    def speed_valid(self, value):
        step = value - 256 if value > 127 else value
        return self.speed_steps[0] <= step <= self.speed_steps[1]

    def home_valid(self, block):
        order = [block[4 + index // 2] >> 4 * (index % 2) & 15 for index in range(HOME_SLOTS)]
        stored = max((count for count in range(1, HOME_SLOTS + 1)
                      if sorted(order[:count]) == list(range(count)) and not any(order[count:])), default=0)
        hidden = block[9] | block[10] << 8
        hideable = (1 << stored) - 1 & ~(1 << self.home_settings_item)
        return stored >= HOME_FIRST_ITEMS and hidden & ~hideable == 0

    def settings_flags_valid(self, version, flags):
        if version < 14:
            return flags == 0
        answer_open = self.reserved_flags & USB_ANSWER_BITS == 0
        return (flags & self.reserved_flags == 0 and flags & 3 < 3 and
                (not answer_open or flags >> 4 & 3 < 3))

    def settings_checksum_field(self, version):
        return 38 if version < 15 else 54

    def settings_checksum_valid(self, data):
        field = self.settings_checksum_field(data[4]) if len(data) > 4 else 38
        return (len(data) >= field + 4 and data[:4] == b"DSST" and
                self.format.fnv(data[:field]) == int.from_bytes(data[field:field + 4], "little"))

    def settings_extension_valid(self, data):
        if data[4] < 15:
            return True
        block = data[38:54]
        offset = int.from_bytes(block[11:13], "little", signed=True) if self.clock_offset_byte else 0
        reserved = list(block[11:])
        if self.clock_offset_byte:
            reserved[0:2] = [0, 0]
        if self.status_items_supported:
            reserved[2] = 0
        return (block[0] >> 3 < 3 and block[1] <= 60 and 5 <= block[2] <= 60 and self.speed_valid(block[3]) and
                -720 <= offset <= 840 and self.home_valid(block) and not any(reserved))

    def show(self, path):
        return git(self.api.ROOT, "show", self.commit + ":" + path)

    def validate(self, data):
        try:
            if len(data) > self.pack_limit:
                raise self.format.PackError(
                    f"the installed firmware accepts at most {self.pack_limit:,} language bytes")
            return self.format.validate_pack(data, self.catalogue, self.drawable)
        except self.format.PackError as error:
            raise self.api.DebugError("language_pack_invalid", str(error)) from error

    def file_info(self, data, repairing=False):
        try:
            if len(data) > self.pack_limit:
                raise self.format.PackError("the pack exceeds the installed language capacity")
            return self.format.pack_info(data)
        except self.format.PackError as error:
            if repairing and len(data) >= self.format.HEADER.size:
                fields = self.format.HEADER.unpack(data[:self.format.HEADER.size])
                magic, version, count, size, catalogue, _, code, name = fields
                if (magic == self.format.MAGIC and version == self.format.VERSION and count > 0 and
                        size == len(data) and size <= self.pack_limit and
                        size > self.format.HEADER.size + 2 * count):
                    try:
                        code = self.format.metadata_text(code, "language code")
                        name = self.format.metadata_text(name, "language name")
                        if not CODE.fullmatch(code):
                            raise self.format.PackError("the replacement target has an invalid code")
                        self.format.check_native_name(name)
                        return {"code": code, "name": name, "count": count,
                                "size": size, "catalogue_hash": catalogue}
                    except self.format.PackError as metadata_error:
                        error = metadata_error
            raise self.api.DebugError("language_pack_invalid", str(error)) from error

    def setting_title_ids(self):
        try:
            source = self.show("firmware/src/app/settings_screens.c")
        except ValueError as error:
            raise self.api.DebugError("language_build_unknown", str(error)) from error
        match = re.search(r"\bCHOICES\s*\[[^]]+\]\s*=\s*\{(.*?)\n\};", source, re.DOTALL)
        names = re.findall(r"\{\s*(STR_[A-Z0-9_]+)\s*,", match.group(1)) if match else []
        known = {name for name, _ in self.catalogue}
        if not names or any(name not in known for name in names):
            raise self.api.DebugError("language_build_unknown", "the installed setting titles are unknown")
        return names

    def english(self):
        return self.validate(self.format.build_pack("en", self.catalogue, {}))

    def metadata(self):
        return {"firmware_build": self.build, "firmware_commit": self.commit,
                "catalogue_hash": self.format.catalogue_hash(self.catalogue),
                "catalogue_count": len(self.catalogue), "pack_capacity": self.pack_limit}


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
            version = data[4] if len(data) > 4 else 0
            snapshot = context["snapshot"]
            if (not snapshot.settings_checksum_valid(data) or
                    not 12 <= version <= snapshot.settings_version or
                    not snapshot.settings_flags_valid(version, data[5]) or
                    not snapshot.settings_extension_valid(data)):
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

    def packs(self, context, repair_code=None, incompatible_files=None):
        snapshot = context["snapshot"]
        entries = [{**snapshot.english(), "source": "builtin", "path": None}]
        seen = set()
        try:
            with directory(context["music"], (self.api.DATA_DIRECTORY, "lang")) as descriptor:
                names = sorted(os.listdir(descriptor))
                if len(names) > DIRECTORY_LIMIT:
                    self.error("language_directory_large", "the language folder has too many entries")
                for name in names:
                    if name.startswith(".") or not name.endswith(".pack"):
                        continue
                    code = self.code(name[:-5])
                    if (code.lower() in seen or
                            (repair_code is not None and code.lower() == repair_code.lower() and code != repair_code)):
                        self.error("language_code_mismatch", "the language code conflicts with an existing FAT filename")
                    seen.add(code.lower())
                    data = read_at(descriptor, name, PACK_LIMIT)
                    metadata = snapshot.file_info(data, repairing=code == repair_code)
                    if metadata["code"] != code:
                        self.error("language_code_mismatch", "the pack filename differs from its language code")
                    path = str(context["music"] / self.api.DATA_DIRECTORY / "lang" / name)
                    if (metadata["count"] != len(snapshot.catalogue) or
                            metadata["catalogue_hash"] != snapshot.format.catalogue_hash(snapshot.catalogue)):
                        if incompatible_files is not None:
                            incompatible_files.append({**metadata, "path": path, "reason": "catalogue_mismatch"})
                        continue
                    if code.lower() == "en":
                        self.error("language_english_protected", "an external English pack would hide the builtin fallback")
                    try:
                        info = snapshot.validate(data)
                    except self.api.DebugError:
                        if code == repair_code:
                            continue
                        raise
                    entries.append({**info, "source": "file", "path": path})
        except FileNotFoundError:
            pass
        if (len(entries) > 8 or len({item["name"].translate(ASCII_LOWER) for item in entries}) != len(entries) or
                len({item["code"].lower() for item in entries}) != len(entries)):
            self.error("language_choices_ambiguous", "the language menu is full or has duplicate native names")
        return entries

    def list(self, music_volume=None):
        context = self.context(music_volume)
        selected = self.selected(context)
        incompatible = []
        languages = [{key: value for key, value in entry.items() if key != "texts"}
                     for entry in self.packs(context, incompatible_files=incompatible)]
        saved = selected
        selected = selected if selected in {entry["code"] for entry in languages} else "en"
        for entry in languages:
            entry["selected"] = entry["code"] == selected
        return {"ok": True, **context["snapshot"].metadata(), "music_volume": str(context["music"]),
                "selected": selected, "saved_language": saved, "languages": languages,
                "incompatible_files": incompatible}

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
            titles = [texts[name].translate(ASCII_LOWER)
                      for name in context["snapshot"].setting_title_ids()]
            names = [entry["name"].translate(ASCII_LOWER) for entry in entries.values()]
            if titles.count(label.translate(ASCII_LOWER)) != 1 or len(set(names)) != len(names):
                self.error("language_choices_ambiguous", "the localized language choices are ambiguous")
            name = self.safe_script_text(entries[code]["name"])
            text += "set " + label + "=" + name + "\nwait 250\n"
        return self.api.prepare_script(text + "reconnect\n", context["snapshot"].build).decode("utf-8")

    def refresh(self, context, code, timeout):
        text = self.selection_text(context, code)
        self.recheck(context)
        runner = self.api.Tool(host=self.tool.host, volume_name=self.tool.volume_name,
                               sleep=self.tool.sleep, clock=self.tool.clock, wall=self.tool.wall)
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
        existing = self.packs(context, repair_code=code)
        if any(item["code"].lower() == code.lower() and item["code"] != code for item in existing):
            self.error("language_code_mismatch", "the language code conflicts with an existing FAT filename")
        if any(item["code"] != code and item["name"].translate(ASCII_LOWER) == info["name"].translate(ASCII_LOWER) for item in existing):
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
