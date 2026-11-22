import argparse
import contextlib
import importlib.util
import json
import os
import re
import string
import stat
import struct
import subprocess
import sys
import time
import unicodedata
from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path

import cover_export
from fnv import FNV_OFFSET, fnv1a
import library_index
from identity import IDENTITY

DATA_FOLDER = IDENTITY.data_directory
MEBIBYTE = 1024 * 1024
WRITE_CHUNK_BYTES = MEBIBYTE
SCAN_MAGIC = b"DSS2"
SCAN_RECORD = struct.Struct("<4sIII16sIIIII")
SCAN_PRINT_BYTES = 16
SCAN_VERSION_BYTES = 16
SCAN_INTERNAL_KNOWN = 1
SCAN_INTERNAL_UNKNOWN = 2
SCAN_GUARD_OFFSET = 512
SCAN_GUARD_BYTES = 32
MUSIC_GENERATION_FILE = "music_gen.bin"
USB_VENDOR = "1209"
USB_PRODUCT = "0001"
INTERNAL_LUN = 0
CARD_LUN = 1
SCSI_ADDRESS = re.compile(r"\d+:\d+:\d+:([01])")
WORD_MASK = 0xFFFFFFFF
SECOND_SEED = FNV_OFFSET ^ 0x5BD1E995
PATH_CAPACITY = 1024
MAX_DEPTH = 16
EXFAT_SIZE_LIMIT = 1 << 32
COUNTED_SUFFIXES = tuple(sorted(suffix.encode("ascii") for suffix in
                                library_index.AUDIO_SUFFIXES | library_index.PLAYLIST_SUFFIXES |
                                {library_index.CUE_SUFFIX}))
ART_NAMES = frozenset(f"{stem}{suffix}".encode("ascii")
                      for stem in cover_export.FOLDER_IMAGE_STEMS
                      for suffix in cover_export.FOLDER_IMAGE_SUFFIXES)
SYSTEM_FOLDER = b"system volume information"
UNIX_EPOCH = datetime(1970, 1, 1)
DOS_FIRST_YEAR = 1980
DOS_LAST_YEAR = 2107
NANOSECONDS = 1_000_000_000
FAT_NAMES = {"fat", "fat12", "fat16", "fat32", "vfat", "msdos"}
EXFAT_NAMES = {"exfat"}
DRIVE_LETTER = re.compile(r"^[A-Za-z]:$")
SEM_FAILCRITICALERRORS = 0x0001
MAC_MOUNT_LINE = re.compile(r"^.* on (.+) \(([^,)]+)(?:, ([^)]*))?\)$")
MOUNT_ESCAPE = re.compile(r"\\([0-7]{3})")
REQUIREMENTS = Path(__file__).resolve().with_name("requirements.txt")
MAC_CAVEAT = ("on a Mac the player's file times are a best guess; after the player has scanned "
              "the card, --check shows whether they match")
LINUX_CAVEAT = ("on Linux the card's file times are read as UTC (or the mount's time_offset); "
                "after the player has scanned the card, --check shows whether they match")
OTHER_CAVEAT = "this isn't a FAT32 or exFAT card, so the file times may not match the player's"


@dataclass(frozen=True)
class CardFile:
    name: str
    standard_size: int
    header_size: int


LIBRARY_FILE = CardFile("library.db", 4 * MEBIBYTE, library_index.HEADER_SIZE)
MAX_LIBRARY_BYTES = 128 * MEBIBYTE
COVERS_FILE = CardFile("covers.bin", 80 * MEBIBYTE, cover_export.HEADER.size)
SCAN_FILE = CardFile("scan.bin", 4096, SCAN_RECORD.size)


@dataclass(frozen=True)
class CardEntry:
    name: bytes
    directory: bool
    size: int
    modified: int
    location: object = None


@dataclass(frozen=True)
class Fingerprint:
    complete: bool
    first: int
    second: int
    entries: int


@dataclass(frozen=True)
class CardVolume:
    file_system: str
    supported: bool
    exfat: bool
    fixed_offset: int
    caveat: str


@dataclass(frozen=True)
class MusicDrive:
    root: Path
    volume: CardVolume


@dataclass(frozen=True)
class MusicDrives:
    card: MusicDrive
    internal: MusicDrive | None = None

    @property
    def internal_root(self):
        return None if self.internal is None else self.internal.root


@dataclass(frozen=True)
class ScanSnapshot:
    card: Fingerprint
    internal: Fingerprint | None = None
    generation: int = 0

    @property
    def complete(self):
        return self.card.complete and (self.internal is None or self.internal.complete)

    @property
    def entries(self):
        return self.card.entries + (0 if self.internal is None else self.internal.entries)


@dataclass
class SyncOptions:
    library: bool = True
    covers: bool = True
    fingerprint: bool = True
    dry_run: bool = False
    fonts: bool = True
    verbose: bool = False


class SyncError(Exception):
    pass


class FingerprintHash:
    def __init__(self):
        self.first = FNV_OFFSET
        self.second = SECOND_SEED
        self.entries = 0

    def mix(self, data):
        self.first = fnv1a(data, self.first)
        self.second = fnv1a(data, self.second)

    def mix_entry(self, folder, entry):
        self.mix(folder + b"/" + entry.name + struct.pack(
            "<III", 1 if entry.directory else 0, entry.size & WORD_MASK,
            entry.modified & WORD_MASK))
        self.entries += 1


def fingerprint_counts(name, directory):
    folded = name.lower()
    if directory:
        return folded != SYSTEM_FOLDER
    return folded in ART_NAMES or any(
        len(folded) > len(suffix) and folded.endswith(suffix) for suffix in COUNTED_SUFFIXES)


def child_path(folder, name):
    return (b"" if folder == b"/" else folder) + b"/" + name


def walk_folder(state, list_folder, folder, entries, depth):
    for entry in entries:
        if entry.name.startswith(b".") or not fingerprint_counts(entry.name, entry.directory):
            continue
        state.mix_entry(folder, entry)
        if not entry.directory:
            continue
        path = child_path(folder, entry.name)
        if depth + 1 >= MAX_DEPTH or len(path) + 1 > PATH_CAPACITY:
            return False
        listing = list_folder(entry.location)
        if listing is not None and not walk_folder(state, list_folder, path, listing, depth + 1):
            return False
    return True


def music_fingerprint(list_folder, root_location):
    state = FingerprintHash()
    listing = list_folder(root_location)
    complete = listing is not None and walk_folder(state, list_folder, b"/", listing, 0)
    return Fingerprint(complete, state.first, state.second, state.entries)


def dos_stamp(local_seconds):
    moment = UNIX_EPOCH + timedelta(seconds=local_seconds)
    if not DOS_FIRST_YEAR <= moment.year <= DOS_LAST_YEAR:
        return 0
    date = (moment.year - DOS_FIRST_YEAR) << 9 | moment.month << 5 | moment.day
    clock = moment.hour << 11 | moment.minute << 5 | moment.second // 2
    return date << 16 | clock


def current_offset():
    return time.localtime().tm_gmtoff


def card_name(name, platform=sys.platform):
    if platform == "win32":
        return name.encode("utf-8", "surrogatepass")
    if platform == "darwin":
        name = unicodedata.normalize("NFC", name)
    return name.encode("utf-8", "surrogateescape")


def card_stamp(stat, volume):
    seconds = stat.st_mtime_ns // NANOSECONDS
    if volume.exfat:
        return seconds // 2 & WORD_MASK
    return dos_stamp(seconds + volume.fixed_offset)


def host_entry(entry, volume):
    stat = entry.stat(follow_symlinks=False)
    directory = entry.is_dir(follow_symlinks=False)
    if volume.exfat and not directory and stat.st_size >= EXFAT_SIZE_LIMIT:
        return None
    return CardEntry(card_name(entry.name), directory, 0 if directory else stat.st_size,
                     card_stamp(stat, volume), entry.path)


def host_lister(volume):
    def list_folder(location):
        try:
            with os.scandir(location) as listing:
                found = [entry for entry in listing if not entry.name.startswith(".")]
        except OSError:
            return None
        entries = (host_entry(entry, volume) for entry in found)
        return [entry for entry in entries if entry is not None]
    return list_folder


def card_fingerprint(card, volume):
    return music_fingerprint(host_lister(volume), str(card))


def scan_record(snapshot, version):
    card = snapshot.card
    internal = snapshot.internal
    tail = ((0, 0, 0, SCAN_INTERNAL_UNKNOWN, 0) if internal is None else
            (internal.first, internal.second, internal.entries, SCAN_INTERNAL_KNOWN,
             snapshot.generation))
    return SCAN_RECORD.pack(SCAN_MAGIC, card.first, card.second, card.entries, version, *tail)


def stored_snapshot(data):
    if len(data) < SCAN_RECORD.size or data[:4] != SCAN_MAGIC:
        return None
    _, first, second, entries, _, inner_first, inner_second, inner_entries, state, generation = (
        SCAN_RECORD.unpack_from(data))
    card = Fingerprint(True, first, second, entries)
    if state == SCAN_INTERNAL_KNOWN:
        return ScanSnapshot(card, Fingerprint(True, inner_first, inner_second, inner_entries),
                            generation)
    return ScanSnapshot(card)


def stored_version(data):
    if len(data) < SCAN_PRINT_BYTES + SCAN_VERSION_BYTES:
        return bytes(SCAN_VERSION_BYTES)
    return bytes(data[SCAN_PRINT_BYTES:SCAN_PRINT_BYTES + SCAN_VERSION_BYTES])


def read_prefix(path, length):
    try:
        with open_card_file(path, writable=False) as file:
            return file.read(length)
    except OSError:
        return b""


def target_size(existing, needed, standard):
    if existing is not None and existing >= needed:
        return existing
    return max(standard, needed)


def existing_size(path):
    try:
        return path.stat().st_size
    except FileNotFoundError:
        return None


def write_all(file, data):
    position = 0
    while position < len(data):
        written = file.write(data[position:])
        if not written:
            raise OSError("the cache stopped accepting data")
        position += written


def zero_fill(file, start, end):
    file.seek(start)
    while start < end:
        length = min(WRITE_CHUNK_BYTES, end - start)
        write_all(file, bytes(length))
        start += length


def settle(file):
    file.flush()
    os.fsync(file.fileno())


def checked_cache_folder(folder):
    if folder.resolve() != folder.absolute():
        raise SyncError(f"{folder} contains a symbolic link. Nothing was written.")
    return folder.stat(follow_symlinks=False)


def same_file(first, second):
    return (first.st_dev, first.st_ino) == (second.st_dev, second.st_ino)


def create_windows_cache(path, writable):
    import ctypes
    from ctypes import wintypes
    import msvcrt

    generic_read, generic_write = 0x80000000, 0x40000000
    share_read_write = 3
    create_new = 1
    open_reparse_point = 0x00200000
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                       wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    create.restype = wintypes.HANDLE
    close = kernel.CloseHandle
    close.argtypes = (wintypes.HANDLE,)
    close.restype = wintypes.BOOL
    access = generic_read | (generic_write if writable else 0)
    handle = create(str(path), access, share_read_write, None, create_new, open_reparse_point, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        mode = os.O_BINARY | (os.O_RDWR if writable else os.O_RDONLY)
        return msvcrt.open_osfhandle(handle, mode)
    except BaseException:
        close(handle)
        raise


@contextlib.contextmanager
def open_card_file(path, create=False, writable=True):
    path = path.absolute()
    parent = checked_cache_folder(path.parent)
    flags = (os.O_RDWR if writable else os.O_RDONLY) | getattr(os, "O_NOFOLLOW", 0)
    directory = None
    descriptor = None
    try:
        if os.open in os.supports_dir_fd:
            directory = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) |
                                getattr(os, "O_NOFOLLOW", 0))
            if not same_file(parent, os.fstat(directory)):
                raise SyncError("The cache folder changed. Nothing was written.")
        leaf = path.name if directory is not None else path
        try:
            descriptor = os.open(leaf, flags, 0o600, dir_fd=directory)
        except FileNotFoundError:
            if not create:
                raise
            if sys.platform == "win32":
                if path.is_symlink():
                    raise SyncError("The cache path changed or is linked. Nothing was written.")
                descriptor = create_windows_cache(path, writable)
            else:
                descriptor = os.open(leaf, flags | os.O_CREAT | os.O_EXCL, 0o600,
                                     dir_fd=directory)
        current = os.fstat(descriptor)
        if (not stat.S_ISREG(current.st_mode) or current.st_nlink != 1 or
                not same_file(current, path.lstat()) or
                not same_file(parent, checked_cache_folder(path.parent))):
            raise SyncError("The cache path changed or is linked. Nothing was written.")
        with os.fdopen(descriptor, "r+b" if writable else "rb") as file:
            descriptor = None
            yield file
    finally:
        if descriptor is not None:
            os.close(descriptor)
        if directory is not None:
            os.close(directory)


def verify_cache(file, data, start=0):
    file.seek(start)
    while start < len(data):
        end = min(start + WRITE_CHUNK_BYTES, len(data))
        if file.read(end - start) != data[start:end]:
            raise OSError("the cache readback did not match")
        start = end


def write_card_file(path, data, card_file):
    header = min(card_file.header_size, len(data))
    with open_card_file(path, create=True) as file:
        size = target_size(os.fstat(file.fileno()).st_size, len(data), card_file.standard_size)
        try:
            write_all(file, bytes(header))
            settle(file)
            file.seek(header)
            write_all(file, data[header:])
            end = file.seek(0, os.SEEK_END)
            zero_fill(file, end, size)
            settle(file)
            verify_cache(file, data, header)
            file.seek(0)
            write_all(file, data[:header])
            settle(file)
            verify_cache(file, data)
        except OSError:
            with contextlib.suppress(OSError):
                file.seek(0)
                write_all(file, bytes(header))
                settle(file)
            raise
    return size


def file_system_name(card):
    if sys.platform == "win32":
        return windows_file_system(card)
    mounts = linux_mounts() if sys.platform.startswith("linux") else mac_mounts()
    mount = mount_for(card, mounts)
    return (mount[1], mount[2]) if mount else (None, "")


def windows_file_system(card):
    import ctypes
    name = ctypes.create_unicode_buffer(64)
    found = ctypes.windll.kernel32.GetVolumeInformationW(
        ctypes.c_wchar_p(card.anchor), None, 0, None, None, None, name, len(name))
    return (name.value if found else None), ""


def mount_field(value):
    return MOUNT_ESCAPE.sub(lambda match: chr(int(match.group(1), 8)), value)


def linux_mount_rows():
    try:
        lines = Path("/proc/self/mounts").read_text(encoding="utf-8",
                                                    errors="surrogateescape").splitlines()
    except OSError:
        return
    for line in lines:
        fields = line.split()
        if len(fields) >= 4:
            yield mount_field(fields[0]), mount_field(fields[1]), fields[2], fields[3]


def linux_mounts():
    return [(point, kind, options) for _, point, kind, options in linux_mount_rows()]


def mac_mounts():
    try:
        output = subprocess.run(["mount"], capture_output=True, text=True, check=False).stdout
    except OSError:
        return []
    mounts = []
    for line in output.splitlines():
        match = MAC_MOUNT_LINE.match(line)
        if match:
            mounts.append((match.group(1), match.group(2), match.group(3) or ""))
    return mounts


def mount_for(card, mounts):
    target = os.path.realpath(card)
    best = None
    for point, kind, options in mounts:
        inside = target == point or target.startswith(point.rstrip("/") + "/")
        if inside and (best is None or len(point) > len(best[0])):
            best = (point, kind, options)
    return best


def mount_option_offset(options):
    for option in options.split(","):
        if option == "tz=UTC":
            return 0
        if option.startswith("time_offset="):
            return int(option.split("=", 1)[1]) * 60
    return 0


def card_volume(name, options="", platform=sys.platform):
    kind = (name or "").lower()
    exfat = kind in EXFAT_NAMES
    if kind not in FAT_NAMES and not exfat:
        return CardVolume(name or "unknown", False, False, current_offset(), OTHER_CAVEAT)
    label = "exFAT" if exfat else "FAT32" if kind == "fat32" else "FAT"
    if exfat:
        return CardVolume(label, True, True, 0, "")
    if platform.startswith("linux"):
        return CardVolume(label, True, False, mount_option_offset(options), LINUX_CAVEAT)
    if platform == "win32":
        return CardVolume(label, True, False, current_offset(), "")
    return CardVolume(label, True, False, current_offset(), MAC_CAVEAT)


def detect_volume(card):
    name, options = file_system_name(card)
    return card_volume(name, options)


def card_root(text):
    if DRIVE_LETTER.match(text):
        text += os.sep
    return Path(text).expanduser().resolve()


def windows_drive_roots():
    import ctypes
    kernel32 = ctypes.windll.kernel32
    kernel32.SetErrorMode(SEM_FAILCRITICALERRORS)
    mask = kernel32.GetLogicalDrives()
    return [Path(f"{letter}:\\") for index, letter in enumerate(string.ascii_uppercase)
            if mask >> index & 1]


def mounted_roots(platform=sys.platform):
    if platform == "win32":
        return windows_drive_roots()
    if platform == "darwin":
        return sorted(Path("/Volumes").glob("*"))
    user = os.environ.get("USER", "")
    return sorted([*Path("/media", user).glob("*"), *Path("/run/media", user).glob("*")])


def player_cards(roots):
    cards = []
    for root in roots:
        try:
            if (root / DATA_FOLDER).is_dir():
                cards.append(root)
        except OSError:
            continue
    return cards


def matching_pairs(mounts):
    players = {}
    for device, lun, mount in mounts:
        players.setdefault(device, {}).setdefault(lun, set()).add(mount)
    for drives in players.values():
        card = drives.get(CARD_LUN, set())
        internal = drives.get(INTERNAL_LUN, set())
        if len(card) == len(internal) == 1 and card != internal:
            yield next(iter(card)), next(iter(internal))


def command_json(command):
    try:
        result = subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace",
                                check=True, timeout=15)
        return json.loads(result.stdout)
    except (OSError, subprocess.SubprocessError, ValueError):
        return []


def mac_usb_devices(tree):
    if isinstance(tree, list):
        for node in tree:
            yield from mac_usb_devices(node)
    elif isinstance(tree, dict):
        if tree.get("_name") == IDENTITY.name:
            vendor, product = (str(tree.get(key, "")).partition(" ")[0].lower().removeprefix("0x")
                               for key in ("vendor_id", "product_id"))
            if (vendor, product) == (USB_VENDOR, USB_PRODUCT):
                yield tree
        for value in tree.values():
            yield from mac_usb_devices(value)


def mac_media_lun(medium):
    if "Logical Unit" in medium:
        unit = str(medium["Logical Unit"]).strip()
        return int(unit) if unit in (str(INTERNAL_LUN), str(CARD_LUN)) else None
    name = medium.get("_name", "").casefold()
    for label, lun in (("internal storage", INTERNAL_LUN), ("sd card", CARD_LUN)):
        if label in name:
            return lun
    return None


def mac_player_mounts():
    tree = command_json(["system_profiler", "SPUSBDataType", "-json"])
    for device, player in enumerate(mac_usb_devices(tree)):
        for medium in player.get("Media", ()):
            lun = mac_media_lun(medium)
            if lun is None:
                continue
            for volume in medium.get("volumes", ()):
                point = volume.get("mount_point")
                if point:
                    yield device, lun, point


def linux_is_player(device):
    fields = ("idVendor", "idProduct", "product")
    actual = tuple((device / field).read_text().strip() for field in fields)
    return actual == (USB_VENDOR, USB_PRODUCT, IDENTITY.name)


def linux_player_lun(source, block_class):
    try:
        if not source.startswith("/dev/"):
            return None
        disk = (block_class / Path(source).resolve().name).resolve(strict=True)
        lun = None
        for parent in disk.parents:
            address = SCSI_ADDRESS.fullmatch(parent.name)
            if address is not None:
                lun = int(address[1])
            vendor = parent / "idVendor"
            if vendor.is_file():
                if lun is not None and linux_is_player(parent):
                    return parent, lun
                return None
    except OSError:
        return None
    return None


def linux_player_mounts():
    block_class = Path("/sys/class/block")
    for source, point, kind, _ in linux_mount_rows():
        if kind in FAT_NAMES | EXFAT_NAMES | {"fuseblk"}:
            device = linux_player_lun(source, block_class)
            if device is not None:
                yield device[0], device[1], point


def windows_player_mounts():
    usb_id = f"USB\\VID_{USB_VENDOR}&PID_{USB_PRODUCT}\\"
    inquiry = f"USBSTOR\\DISK&VEN_{IDENTITY.id}&"
    script = f"$player = '{usb_id}'\n$inquiry = '{inquiry}'\n" + r"""
function ParentPlayer($id) {
  for ($n = 0; $n -lt 10 -and $id; $n++) {
    if ($id.StartsWith($player, [StringComparison]::OrdinalIgnoreCase)) { return $id }
    $id = (Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_Parent -ErrorAction SilentlyContinue).Data
  }
}
$rows = @(foreach ($disk in (Get-CimInstance Win32_DiskDrive)) {
  if (!$disk.PNPDeviceID -or $null -eq $disk.SCSILogicalUnit) { continue }
  if (!$disk.PNPDeviceID.StartsWith($inquiry, [StringComparison]::OrdinalIgnoreCase)) { continue }
  $device = ParentPlayer $disk.PNPDeviceID
  if (!$device) { continue }
  foreach ($partition in (Get-Partition -DiskNumber $disk.Index -ErrorAction SilentlyContinue)) {
    if ($partition.DriveLetter) {
      [PSCustomObject]@{device=$device; lun=[int]$disk.SCSILogicalUnit; root="$($partition.DriveLetter):\"}
    }
  }
})
ConvertTo-Json -InputObject $rows -Compress
"""
    rows = command_json(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script])
    if isinstance(rows, list):
        for row in rows:
            yield row["device"], row["lun"], row["root"]


def mounted_player_drives():
    if sys.platform == "darwin":
        mounts = mac_player_mounts()
    elif sys.platform == "win32":
        mounts = windows_player_mounts()
    elif sys.platform.startswith("linux"):
        mounts = linux_player_mounts()
    else:
        return []
    return [(device, lun, card_root(root)) for device, lun, root in mounts
            if lun in (CARD_LUN, INTERNAL_LUN)]


def typed_path(text, platform=sys.platform):
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "\"'":
        return text[1:-1]
    if platform != "win32":
        return text.replace("\\ ", " ")
    return text


def ask_for_card(cards, read=input):
    if len(cards) == 1:
        answer = typed_path(read(f"Card to sync [{cards[0]}]: "))
        return answer or str(cards[0])
    if cards:
        print(f"Cards that have been in a {IDENTITY.name} player: " +
              ", ".join(str(card) for card in cards))
    answer = typed_path(read("Card drive or folder (for example E: or /Volumes/CARD; you can "
                             "drag it here): "))
    if not answer:
        raise SyncError("No card given. Nothing was written.")
    return answer


def check_target(card, volume, echo_exists):
    if not card.is_dir():
        raise SyncError(f"{card} isn't a folder or drive. Give the card's drive, for example E:\\")
    if echo_exists:
        return
    if not os.path.ismount(card):
        raise SyncError(f"{card} isn't the top of a memory card. The player reads music from the "
                        "whole card, so give the card's drive (for example E:\\ on Windows or "
                        "/Volumes/CARD on a Mac).")
    if not volume.supported:
        raise SyncError(f"{card} is {volume.file_system}, not a FAT32 or exFAT memory card.")


def ensure_data_folder(folder, options, assume_yes):
    if folder.resolve() != folder.absolute():
        raise SyncError(f"{folder} contains a symbolic link. Nothing was written.")
    if folder.is_dir():
        return
    if options.dry_run:
        print(f"Would create {folder}")
        return
    if not assume_yes:
        if not sys.stdin.isatty():
            raise SyncError(f"{folder} doesn't exist yet. Run again with --yes to create it.")
        answer = input(f"{folder} doesn't exist yet (the player hasn't used this card). "
                       "Create it? [y/N] ")
        if answer.strip().lower() not in ("y", "yes"):
            raise SyncError("Nothing was written.")
    folder.mkdir()


def require_dependencies():
    packages = {"mutagen": "mutagen", "PIL": "Pillow", "fontTools": "fonttools"}
    missing = [package for module, package in packages.items()
               if importlib.util.find_spec(module) is None]
    if missing:
        raise SyncError(f"{', '.join(missing)} missing. Install the tools' requirements with: "
                        f'"{sys.executable}" -m pip install -r "{REQUIREMENTS}"')


def step(text):
    print(f"{text:<28}", end="", flush=True)


def human_size(size):
    if size >= MEBIBYTE:
        return f"{size / MEBIBYTE:.1f} MB"
    return f"{max(1, round(size / 1024))} KB"


def plural(count, word, words=None):
    return f"{count:,} {word if count == 1 else words or word + 's'}"


def build_library(card, internal=None):
    step("Reading the music...")
    try:
        tracks, playlists, failures = library_index.scan(card, internal)
    except ValueError as error:
        raise SyncError(str(error)) from error
    model = library_index.build_library(tracks, playlists)
    try:
        data = library_index.encode_library(model)
    except ValueError as error:
        raise SyncError(f"the library is too big for the player: {error}") from error
    print(f"{plural(len(model.tracks), 'track')}, {plural(len(model.albums), 'album')}, "
          f"{plural(len(model.playlists), 'playlist')}")
    for failure in failures:
        print(f"  skipped (tags unreadable) {failure}")
    for playlist in model.playlists:
        if playlist.unresolved:
            missing = plural(playlist.unresolved, "entry", "entries")
            print(f"  playlist {playlist.name}: {missing} not found")
    return model, data


def current_library(data):
    prefix = (library_index.MAGIC, library_index.VERSION, 10)
    return (len(data) >= library_index.HEADER_PREFIX.size and
            library_index.HEADER_PREFIX.unpack_from(data) == prefix)


def cached_album_sources(path):
    with open_card_file(path, writable=False) as file:
        if os.fstat(file.fileno()).st_size > MAX_LIBRARY_BYTES:
            raise ValueError("the cached library is too large")
        data = file.read()
    if len(data) < library_index.HEADER_SIZE or not current_library(data):
        raise ValueError("the cached library is incomplete or unsupported")
    widths = (library_index.TRACK_RECORD.size, library_index.ALBUM_RECORD.size,
              library_index.GROUP_RECORD.size, library_index.GROUP_RECORD.size,
              library_index.GROUP_RECORD.size, library_index.FOLDER_RECORD.size,
              library_index.INDEX_RECORD.size, library_index.GROUP_RECORD.size,
              library_index.INDEX_RECORD.size, 1)
    for index, width in enumerate(widths):
        offset, count, stride, music = library_index.SECTION_ENTRY.unpack_from(
            data, library_index.HEADER_PREFIX.size + index * library_index.SECTION_ENTRY.size)
        if (offset < library_index.HEADER_SIZE or stride != width or music > count or
                offset + count * stride > len(data) or (index < 8 and count > library_index.MAX_U16)):
            raise ValueError("the cached library section is invalid")
    return cover_export.album_sources_from_library(data)


def album_sources(card, model, library_path, internal=None):
    if model is not None:
        return cover_export.album_sources(model)
    try:
        return cached_album_sources(library_path)
    except (OSError, ValueError, struct.error):
        print("(no library on the card yet, reading the music first)")
        return cover_export.album_sources(build_library(card, internal)[0])


def art_describer(volume, internal=None):
    if internal is None and not volume.supported:
        return None

    def describe(path):
        source = volume
        if internal is not None and path.is_relative_to(internal.root):
            source = internal.volume
        if source.supported:
            return card_name(path.name), card_stamp(path.stat(), source)
        return None
    return describe


def covers_capacity(path, count):
    return target_size(existing_size(path), cover_export.stamped_size(count),
                       COVERS_FILE.standard_size)


def check_music_paths(card):
    folders = [card]
    while folders:
        with os.scandir(folders.pop()) as listing:
            for entry in listing:
                if entry.name.startswith("."):
                    continue
                if entry.is_symlink():
                    raise SyncError(f"{entry.path} is a symbolic link. Nothing was written.")
                if entry.is_dir(follow_symlinks=False):
                    folders.append(Path(entry.path))


def check_album_paths(card, sources, internal=None):
    for source in sources:
        for relative in source.paths:
            parts = relative.split("/")
            root = card
            if library_index.drive_root(relative):
                if internal is None:
                    raise SyncError("The library includes music on the player's own storage. "
                                    "Connect the player to update its covers.")
                root = internal
                parts = parts[1:]
            path = cover_export.music_path(card, relative, internal)
            if (not parts or any(part in ("", ".", "..") for part in parts) or
                    "\\" in relative or ":" in relative or "\x00" in relative or
                    not path.resolve().is_relative_to(root.resolve())):
                raise SyncError("The library contains a path outside its music drive. Rebuild it first.")


def build_covers(drives, sources, options):
    card = drives.card.root
    check_album_paths(card, sources, drives.internal_root)
    step("Finding album covers...")
    covers = cover_export.collect_covers(card, sources, cover_export.cover_record,
                                         art_describer(drives.card.volume, drives.internal),
                                         internal=drives.internal_root)
    capacity = covers_capacity(card / DATA_FOLDER / COVERS_FILE.name, len(covers.records))
    data = cover_export.encode_stamped(covers.records, covers.stamps, capacity)
    print(f"{plural(len(covers.records), 'cover')}, {len(covers.missing):,} without art")
    for album in covers.missing if options.verbose else ():
        print(f"  no cover: {album}")
    return data


def take_fingerprint(card, volume):
    try:
        return card_fingerprint(card, volume)
    except OSError as error:
        print(f"  couldn't read {error.filename}: {error.strerror}")
        return Fingerprint(False, 0, 0, 0)


def take_snapshot(drives):
    fingerprint = take_fingerprint(drives.card.root, drives.card.volume)
    internal = drives.internal
    if internal is None:
        return ScanSnapshot(fingerprint)
    generation = read_prefix(internal.root / DATA_FOLDER / MUSIC_GENERATION_FILE, 4)
    return ScanSnapshot(fingerprint, take_fingerprint(internal.root, internal.volume),
                        int.from_bytes(generation, "little") if len(generation) == 4 else 0)


def write_or_report(path, data, card_file, options):
    existing = existing_size(path)
    size = target_size(existing, len(data), card_file.standard_size)
    if options.dry_run:
        place = "a new" if existing is None else "the" if existing == size else "a larger"
        print(f"  would write {card_file.name}: {human_size(len(data))} into {place} "
              f"{human_size(size)} file")
        return
    try:
        write_card_file(path, data, card_file)
    except OSError as error:
        raise SyncError(f"couldn't write {path}: {error.strerror or error}") from error
    print(f"  {card_file.name:<11} {human_size(len(data))} in a {human_size(size)} file")


def verified_snapshot(drives, before):
    step("Checking the music again...")
    after = take_snapshot(drives)
    if not (before.complete and after.complete):
        print("incomplete")
        print(f"  The music couldn't be fingerprinted completely. The player can check up to "
              f"{MAX_DEPTH - 1} folder levels below the root and paths up to "
              f"{PATH_CAPACITY - 1:,} bytes.")
        return None
    if before != after:
        print("changed")
        print("  The music changed while this ran. The player will check it at start-up.")
        return None
    print(f"{after.entries:,} files and folders, unchanged")
    return after


def publish_fingerprint(path, record, fallback, options):
    try:
        write_or_report(path, record, SCAN_FILE, options)
    except (OSError, SyncError, KeyboardInterrupt):
        write_or_report(path, fallback, SCAN_FILE, options)
        raise


def clear_scan_guard(path, options):
    if options.dry_run:
        return
    try:
        with open_card_file(path) as file:
            size = os.fstat(file.fileno()).st_size
            if size <= SCAN_GUARD_OFFSET:
                return
            file.seek(SCAN_GUARD_OFFSET)
            write_all(file, bytes(min(SCAN_GUARD_BYTES, size - SCAN_GUARD_OFFSET)))
            settle(file)
    except FileNotFoundError:
        return
    except OSError as error:
        raise SyncError(f"couldn't write {path}: {error.strerror or error}") from error


def check_cache_targets(folder):
    try:
        checked_cache_folder(folder)
    except FileNotFoundError:
        return
    for card_file in (LIBRARY_FILE, COVERS_FILE, SCAN_FILE):
        try:
            with open_card_file(folder / card_file.name, writable=False):
                pass
        except FileNotFoundError:
            continue


def sync(drives, options):
    card, root = drives.card.root, drives.internal_root
    check_music_paths(card)
    if root is not None:
        check_music_paths(root)
    folder = card / DATA_FOLDER
    check_cache_targets(folder)
    library_path = folder / LIBRARY_FILE.name
    before = None
    if options.library and options.fingerprint:
        step("Fingerprinting the music...")
        before = take_snapshot(drives)
        print(f"{before.entries:,} files and folders" if before.complete else "incomplete")
    model, library_data, covers_data = None, None, None
    if options.library:
        model, library_data = build_library(card, root)
    if options.covers:
        sources = album_sources(card, model, library_path, root)
        covers_data = build_covers(drives, sources, options)
    print(f"{'Planned for' if options.dry_run else 'Writing'} {folder}")
    scan_path = folder / SCAN_FILE.name
    if library_data is not None or covers_data is not None:
        clear_scan_guard(scan_path, options)
    if library_data is not None:
        version = stored_version(read_prefix(scan_path, SCAN_RECORD.size))
        fallback = scan_record(ScanSnapshot(Fingerprint(False, 0, 0, 0)), version)
        write_or_report(scan_path, fallback, SCAN_FILE, options)
        write_or_report(library_path, library_data, LIBRARY_FILE, options)
    if covers_data is not None:
        write_or_report(folder / COVERS_FILE.name, covers_data, COVERS_FILE, options)
    update_fonts(drives, library_data, options)
    if before is None:
        return False
    verified = verified_snapshot(drives, before)
    if verified is None:
        return False
    publish_fingerprint(scan_path, scan_record(verified, version), fallback, options)
    return True


def update_fonts(drives, library, options):
    if not options.fonts:
        return
    step("Fonts for the player...")
    if drives.internal is None:
        print("skipped: connect the player over USB to update them")
        return
    import font_recipe
    volumes = font_recipe.Volumes(drives.internal.root, drives.card.root)
    try:
        report = font_recipe.rebuild(volumes, font_recipe.Extras(library=library),
                                     font_recipe.Options(dry_run=options.dry_run))
    except (font_recipe.RecipeError, font_recipe.glyph_pack.PackError, OSError) as error:
        print(f"not updated: {error}")
        return
    print(report.summary())
    for note in report.skipped:
        print(f"  skipped {note}")


def print_next_steps(options, recorded, drives, seconds):
    volume, internal = drives.card.volume, drives.internal
    print(f"Done in {seconds:.0f} s.")
    if options.dry_run:
        print("Nothing was written (--dry-run).")
        return
    if internal is None:
        print("Next: eject the card safely. If it's in a card reader, put it back in the player.")
    else:
        print("Next: eject both drives safely.")
    if recorded:
        if internal is None:
            print("The card fingerprint was saved. The player will check its own storage once.")
        else:
            print("The fingerprints for both drives were saved.")
        caveats = (volume.caveat,) if internal is None else (volume.caveat, internal.volume.caveat)
        for caveat in dict.fromkeys(caveats):
            if caveat:
                print(f"(Note: {caveat}.)")
    elif options.library:
        print("The player will check the music at start-up.")
    if options.library and not options.covers:
        print("Covers were left as they were: new albums show without a cover until the next "
              "rescan on the player (Settings, Rescan library).")


def run_check(drives):
    card, internal = drives.card.root, drives.internal
    print(f"Checking {card} ({drives.card.volume.file_system})")
    computed = take_snapshot(drives)
    stored = stored_snapshot(read_prefix(card / DATA_FOLDER / SCAN_FILE.name, SCAN_RECORD.size))
    print(f"  this computer: {describe(computed.card)}")
    print(f"  the player's record: {describe(stored.card if stored else None)}")
    if internal is not None:
        print(f"Checking {internal.root} ({internal.volume.file_system})")
        print(f"  this computer: {describe(computed.internal)}, generation {computed.generation}")
        print(f"  the player's record: {describe(stored.internal if stored else None)}, "
              f"generation {stored.generation if stored else 0}")
    library = read_prefix(card / DATA_FOLDER / LIBRARY_FILE.name, library_index.HEADER_PREFIX.size)
    if (stored is not None and computed.complete and computed.card == stored.card and
            (internal is None or computed == stored)):
        if not current_library(library):
            print("They match, but the card's library is missing, damaged or from another version: "
                  "the player will rebuild it after USB.")
            return 1
        print("The card fingerprint matches." if internal is None else "Both fingerprints match.")
        return 0
    if stored is None:
        print("The card has no fingerprint yet, so the player will rescan it after USB.")
        return 1
    print("The records differ. The player may need to check the music or rebuild its library.")
    return 1


def describe(fingerprint):
    if fingerprint is None:
        return "none"
    if not fingerprint.complete:
        return "incomplete"
    return (f"{fingerprint.first:08x} {fingerprint.second:08x}, "
            f"{fingerprint.entries:,} files and folders")


def options_from(arguments):
    return SyncOptions(library=not arguments.covers_only, covers=not arguments.no_covers,
                       fingerprint=not arguments.no_fingerprint, dry_run=arguments.dry_run,
                       verbose=arguments.verbose,
                       fonts=not (arguments.no_fonts or arguments.covers_only))


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        description=f"Build the {IDENTITY.name} library and album covers on the card, "
                    "including internal music when both drives are available, and the "
                    "player's extra fonts on its internal storage.")
    parser.add_argument("card", nargs="?",
                        help="the card's drive or mount point, for example E:\\ or "
                             "/Volumes/CARD; without it, the tool asks")
    parser.add_argument("--internal", metavar="PATH",
                        help="require this mounted internal drive to belong to the same player as the card")
    parts = parser.add_mutually_exclusive_group()
    parts.add_argument("--no-covers", action="store_true",
                       help="build the library only and leave covers.bin as it is")
    parts.add_argument("--covers-only", action="store_true",
                       help="rebuild covers.bin from the card's library.db, or from the music")
    parser.add_argument("--no-fingerprint", action="store_true",
                        help="skip computing fingerprints and ask the player to check at start-up")
    parser.add_argument("--no-fonts", action="store_true",
                        help="leave the player's extra fonts as they are")
    parser.add_argument("--check", action="store_true",
                        help="compare fingerprints for the available drives and check the card's "
                             "library format header without writing files")
    parser.add_argument("--dry-run", action="store_true",
                        help="do everything except writing to the drives")
    parser.add_argument("--yes", action="store_true",
                        help=f"create the card's {DATA_FOLDER} folder without asking")
    parser.add_argument("--verbose", action="store_true", help="list the albums without art")
    return parser.parse_args(argv)


def is_internal_target(card, mounts):
    if any(lun == INTERNAL_LUN and card.is_relative_to(root) for _, lun, root in mounts):
        return True
    for root in (card, *card.parents):
        try:
            (root / DATA_FOLDER / MUSIC_GENERATION_FILE).lstat()
        except FileNotFoundError:
            continue
        return True
    return False


def card_argument(arguments, mounts=()):
    if arguments.card is not None:
        return arguments.card
    if not sys.stdin.isatty():
        raise SyncError("Give the card's drive or mount point, for example E: or /Volumes/CARD.")
    cards = [root for _, lun, root in mounts if lun == CARD_LUN]
    known = [root.resolve() for root in player_cards(mounted_roots())]
    choices = [root for root in dict.fromkeys((*cards, *known))
               if not is_internal_target(root, mounts)]
    return ask_for_card(choices)


def drive_roots(arguments):
    mounts = mounted_player_drives()
    card = card_root(card_argument(arguments, mounts))
    if is_internal_target(card, mounts):
        raise SyncError(f"{card} is the player's internal storage. Select the memory card.")
    matched = [internal for target, internal in matching_pairs(mounts) if target == card]
    if len(matched) > 1:
        raise SyncError("Could not identify the card and internal storage of the same player.")
    internal = matched[0] if matched else None
    if arguments.internal is not None and card_root(arguments.internal) != internal:
        raise SyncError("--internal must match the detected internal storage of the same player "
                        "as the card. Connect the player over USB with both drives mounted.")
    if internal is not None and internal.is_relative_to(card):
        raise SyncError("The card and internal storage must have separate mount points.")
    return card, internal


def wait_before_closing():
    try:
        input("Press Enter to close.")
    except (EOFError, KeyboardInterrupt):
        pass


def main(argv=None):
    arguments = parse_arguments(argv)
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    try:
        return run(arguments)
    except (EOFError, KeyboardInterrupt):
        print("\nStopped. Nothing more was written.", file=sys.stderr)
        return 1
    finally:
        if arguments.card is None and sys.stdin.isatty():
            wait_before_closing()


def run(arguments):
    started = time.monotonic()
    try:
        card, root = drive_roots(arguments)
        volume = detect_volume(card)
        internal = None if root is None else MusicDrive(root, detect_volume(root))
        drives = MusicDrives(MusicDrive(card, volume), internal)
        folder = card / DATA_FOLDER
        check_target(card, volume, folder.is_dir())
        if arguments.check:
            return run_check(drives)
        require_dependencies()
        options = options_from(arguments)
        print(f"{IDENTITY.name} sync: {card} ({volume.file_system})")
        if internal is not None:
            print(f"Internal storage: {internal.root} ({internal.volume.file_system})")
        ensure_data_folder(folder, options, arguments.yes)
        recorded = sync(drives, options)
    except (SyncError, OSError) as error:
        print(f"echo_sync: {error}", file=sys.stderr)
        return 2
    print_next_steps(options, recorded, drives, time.monotonic() - started)
    return 0


if __name__ == "__main__":
    sys.exit(main())
