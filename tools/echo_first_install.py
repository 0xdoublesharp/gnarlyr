import hashlib
import os
from pathlib import Path
import plistlib
import re
import struct
import subprocess
import sys

import echo_debug as debug
import make_image

STOCK_UPDATE_FILE = "ECHOECHO.img"
FAT32_MAX_CLUSTER = 0x0FFFFFF8


def fat_volume_layout(root):
    cluster_bytes = 0
    max_end_cluster = 0
    used_bytes = 0
    entries = []
    queue = [(root, 0)]
    while queue and len(entries) < 20_000:
        current, depth = queue.pop(0)
        if depth > 8:
            continue
        try:
            children = list(current.iterdir())
        except OSError:
            continue
        for child in children:
            try:
                info = child.lstat()
            except OSError:
                continue
            if child.is_symlink():
                continue
            is_dir = child.is_dir()
            size = info.st_size
            if is_dir and 512 <= size <= 32768 and (size & (size - 1)) == 0:
                cluster_bytes = min(cluster_bytes, size) if cluster_bytes else size
            entries.append((info.st_ino, size))
            if is_dir and depth < 8:
                queue.append((child, depth + 1))
    cluster = cluster_bytes or 4096
    for ino, size in entries:
        rounded = ((max(size, 1) + cluster - 1) // cluster) * cluster
        used_bytes += rounded
        if 2 <= ino < FAT32_MAX_CLUSTER:
            span = max(1, (max(size, 1) + cluster - 1) // cluster)
            end = ino + span - 1
            if end > max_end_cluster:
                max_end_cluster = end
    return cluster, max_end_cluster, used_bytes


class MacInstallIO:
    def __init__(self):
        if sys.platform != "darwin":
            raise debug.DebugError("platform_not_supported", "First-install preparation currently requires macOS")

    def allocate(self, stream, size, contiguous=True):
        import fcntl
        flags = 6 if contiguous else 4  # F_ALLOCATECONTIG|F_ALLOCATEALL vs F_ALLOCATEALL
        try:
            request = struct.pack("=IIqqq", flags, 3, 0, size, 0)
            allocated = struct.unpack("=IIqqq", fcntl.fcntl(stream.fileno(), 42, request))[4]
        except OSError as error:
            raise debug.DebugError("contiguous_allocation_failed", str(error)) from error
        if allocated < size:
            raise debug.DebugError("contiguous_allocation_failed", "The whole image could not be allocated contiguously")
        return allocated

    def digest(self, path):
        import fcntl
        with path.open("rb") as stream:
            fcntl.fcntl(stream.fileno(), 48, 1)
            return hashlib.file_digest(stream, "sha256").hexdigest()

    def info(self, volume):
        try:
            return plistlib.loads(subprocess.check_output(["diskutil", "info", "-plist", str(volume)],
                                                         stderr=subprocess.PIPE))
        except (subprocess.CalledProcessError, plistlib.InvalidFileException) as error:
            raise debug.DebugError("volume_not_identified", "Could not identify the player volume") from error

    def remount(self, volume):
        before = self.info(volume)
        device = before["DeviceIdentifier"]
        try:
            subprocess.run(["diskutil", "unmount", str(volume)], check=True, capture_output=True)
            subprocess.run(["diskutil", "mount", device], check=True, capture_output=True)
            after = self.info(device)
        except subprocess.CalledProcessError as error:
            raise debug.DebugError("volume_remount_failed", "Could not remount the player; no drives were ejected") from error
        if (any(after.get(key) != before.get(key) for key in ("DeviceIdentifier", "TotalSize", "VolumeUUID"))
                or not after.get("MountPoint") or not after.get("Writable")):
            raise debug.DebugError("volume_identity_changed", "The remounted volume no longer matches the player")
        return Path(after["MountPoint"])


def stock_player(tool):
    def matches(entry):
        try:
            ids = (debug.number(str(entry.get("vendor_id", "0")).split()[0]),
                   debug.number(entry.get("product_id")))
            return entry.get("_name") == "ECHO" and ids == (0x071b, 0x3203)
        except ValueError:
            return False

    players = [entry for entry in debug.usb_entries(tool.host.usb_tree()) if matches(entry)]
    if not players:
        raise debug.DebugError("stock_player_not_connected", "Connect one full-size ECHO running FiiO in USB storage mode")
    if len(players) != 1:
        raise debug.DebugError("several_players", "Connect only one full-size ECHO")
    return players[0]


def mounted_volumes(player):
    internal, others = [], []
    for medium in player.get("Media", []):
        lun = debug.number(medium.get("Logical Unit", -1))
        volumes = [volume for volume in debug.medium_volumes(medium) if volume["mount_point"]]
        if lun == 0:
            if debug.number(medium.get("size_in_bytes")) not in debug.INTERNAL_BYTES:
                raise debug.DebugError("internal_drive_not_identified", "Unexpected internal storage size")
            internal.extend(volumes)
        elif lun == 1:
            others.extend(volumes)
        elif volumes:
            raise debug.DebugError("internal_drive_not_identified", "Unexpected storage unit on the player")
    if len(internal) != 1:
        raise debug.DebugError("internal_drive_not_identified", "Expected one mounted internal volume on storage unit 0")
    return Path(internal[0]["mount_point"]), [Path(item["mount_point"]) for item in others]


def validate(data):
    build = debug.image_build(data)
    if (len(data) != make_image.IMAGE_SIZE or make_image.word(data, make_image.FW_SIZE_OFFSET) != make_image.FW_SIZE
            or make_image.validate_image(data)):
        raise debug.DebugError("not_an_image", "The image size, header copy or firmware checksum is invalid")
    return build


def check_pending(root):
    for path in root.iterdir():
        name = path.name.upper()
        numbered = re.fullmatch(r"ECHOV(\d{3})\.IMG", name)
        if (name in (debug.UPDATE_FILE.upper(), "ECHOECHO.IMG", "BOTHSLOT.REQ", "MASKROM.REQ")
                or numbered and 100 <= int(numbered[1]) <= 498):
            raise debug.DebugError("pending_update", f"Remove or preserve the existing {path.name} before preparing another update")


def prepare(tool, image):
    if tool.volume is not None:
        raise debug.DebugError("usb_required", "First install requires USB identification; --volume is not accepted")
    source = Path(image)
    if not source.is_file():
        raise debug.DebugError("image_missing", f"{image} is not a file")
    data = source.read_bytes()
    build = validate(data)
    digest = hashlib.sha256(data).hexdigest()
    io = tool.host.first_install_io()
    player = stock_player(tool)
    root, cards = mounted_volumes(player)
    if root.is_symlink() or not root.is_dir() or tool.volume_name and root.name != tool.volume_name:
        raise debug.DebugError("internal_drive_not_identified", "The internal mount does not match the requested player")
    check_pending(root)
    target = root / STOCK_UPDATE_FILE
    created, remounting = False, False
    is_msdos = False
    if hasattr(io, "info"):
        try:
            is_msdos = io.info(root).get("FilesystemType") == "msdos"
        except debug.DebugError:
            is_msdos = False
    try:
        if is_msdos:
            # macOS UserFS msdos silently ignores F_ALLOCATECONTIG and scatters
            # files into low free holes. Hold an open F_PREALLOCATE pad file
            # covering the fragmented prefix so ECHOECHO.img lands past every
            # existing cluster (proven by post-close lstat().st_ino).
            cluster, max_end, used = fat_volume_layout(root)
            stat = os.statvfs(root)
            free_bytes = stat.f_bavail * stat.f_frsize
            max_pad = max(0, free_bytes - 16 * 1024 * 1024)
            pad_bytes = (
                min(max_pad, max(32 * 1024 * 1024, max_end * cluster - used + 32 * 1024 * 1024))
                if max_end > 0 and max_pad > 0 else 0
            )
            pad_path = root / f".first-install-pad-{os.getpid()}.tmp"
            pad_sidecar = root / f"._{pad_path.name}"
            allocated = 0
            try:
                for _ in range(4):
                    pad_stream = None
                    try:
                        if pad_bytes > 0:
                            pad_stream = pad_path.open("xb")
                            io.allocate(pad_stream, pad_bytes, contiguous=False)
                        with target.open("xb") as stream:
                            created = True
                            allocated = io.allocate(stream, len(data))
                            stream.write(data)
                            stream.flush()
                            os.fsync(stream.fileno())
                    finally:
                        if pad_stream is not None:
                            pad_stream.close()
                        pad_path.unlink(missing_ok=True)
                        pad_sidecar.unlink(missing_ok=True)
                    start_cluster = target.lstat().st_ino
                    if max_end == 0 or (max_end < start_cluster < FAT32_MAX_CLUSTER):
                        (root / f"._{STOCK_UPDATE_FILE}").unlink(missing_ok=True)
                        break
                    target.unlink(missing_ok=True)
                    created = False
                    if pad_bytes >= max_pad or max_pad <= 0:
                        raise debug.DebugError(
                            "contiguous_allocation_failed",
                            "The image could not be placed in contiguous tail storage; free space and retry",
                        )
                    pad_bytes = min(max_pad, max(pad_bytes * 2, 64 * 1024 * 1024))
                else:
                    raise debug.DebugError(
                        "contiguous_allocation_failed",
                        "The image could not be placed in contiguous tail storage; free space and retry",
                    )
            finally:
                pad_path.unlink(missing_ok=True)
                pad_sidecar.unlink(missing_ok=True)
        else:
            with target.open("xb") as stream:
                created = True
                allocated = io.allocate(stream, len(data))
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
        if io.digest(target) != digest:
            raise debug.DebugError("copy_mismatch", "The uncached image readback does not match")
        remounting = True
        root = io.remount(root)
        connected = stock_player(tool)
        current_root, cards = mounted_volumes(connected)
        if connected.get("location_id") != player.get("location_id") or current_root != root:
            raise debug.DebugError("volume_identity_changed", "The remounted drive no longer belongs to the same player")
        target = root / STOCK_UPDATE_FILE
        remounting = False
        if io.digest(target) != digest:
            raise debug.DebugError("copy_mismatch", "The uncached image readback after remount does not match")
    except BaseException:
        if created and not remounting:
            target.unlink(missing_ok=True)
        raise
    for volume in (root, *cards):
        tool.host.eject(str(volume))
    return {"ok": True, "state": "ready_for_restart", "image_build": build, "update_file": str(target),
            "bytes": len(data), "sha256": digest, "contiguous_bytes": allocated,
            "verified_after_remount": True, "ejected": [str(volume) for volume in (root, *cards)]}
