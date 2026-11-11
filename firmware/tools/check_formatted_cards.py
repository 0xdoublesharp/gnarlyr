import argparse
import plistlib
import struct
import subprocess
import tempfile
from pathlib import Path


def run(*command):
    return subprocess.run(command, capture_output=True, check=True)


def check_image(image, mountpoint, exfat):
    reply = run("hdiutil", "attach", "-nomount", "-nobrowse", "-plist",
                "-imagekey", "diskimage-class=CRawDiskImage", str(image))
    entities = plistlib.loads(reply.stdout)["system-entities"]
    devices = [entry["dev-entry"] for entry in entities if "dev-entry" in entry]
    disk = devices[0]
    partition = next(device for device in devices if device.endswith("s1"))
    try:
        result = run("fsck_exfat" if exfat else "fsck_msdos", "-n",
                     partition.replace("/dev/disk", "/dev/rdisk"))
        output = (result.stdout + result.stderr).decode()
        if exfat and "appears to be OK" not in output:
            raise ValueError(output)
        if not exfat and any(line.rstrip().endswith("no") for line in output.splitlines()):
            raise ValueError(output)
        print(output.strip(), flush=True)
        run("diskutil", "mount", "-mountPoint", str(mountpoint), partition)
        copied = mountpoint / "copied.wav"
        if copied.read_bytes() != bytes([0x5A]) * 4096:
            raise ValueError("native file did not read back through macOS")
        probe = mountpoint / "host-write.bin"
        probe.write_bytes(bytes(range(256)) * 16)
        if probe.read_bytes() != bytes(range(256)) * 16:
            raise ValueError("macOS write did not read back")
        probe.unlink()
        run("diskutil", "unmount", partition)
        result = run("fsck_exfat" if exfat else "fsck_msdos", "-n",
                     partition.replace("/dev/disk", "/dev/rdisk"))
        print((result.stdout + result.stderr).decode().strip(), flush=True)
    finally:
        run("hdiutil", "detach", disk)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    directory = args.directory.resolve()
    if not directory.is_relative_to(Path("/Volumes/Scratch/SnowskyEcho")):
        raise ValueError("card images must live on Scratch")
    directory.mkdir(parents=True, exist_ok=True)
    for gib in (2, 16, 32, 64, 256):
        with tempfile.TemporaryDirectory(prefix="format-", dir=directory) as temporary:
            root = Path(temporary)
            image = root / "card.img"
            mountpoint = root / "mount"
            mountpoint.mkdir()
            sectors = gib * 2 * 1024 * 1024
            subprocess.run([str(args.tool), str(sectors), str(image)], check=True)
            with image.open("rb") as handle:
                mbr = handle.read(512)
            start, length = struct.unpack_from("<II", mbr, 454)
            if start + length != sectors:
                raise ValueError("partition bounds do not match the card")
            check_image(image, mountpoint, gib > 32)
            print(f"ok {gib} GiB: fsck and macOS read/write", flush=True)


if __name__ == "__main__":
    main()
