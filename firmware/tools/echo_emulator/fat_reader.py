"""Reads files back from the emulated internal drive's FAT32 volume, as a host would."""
import struct
from pathlib import Path

SECTOR = 512
ENTRY_BYTES = 32
ATTRIBUTE_DIRECTORY = 0x10
ATTRIBUTE_VOLUME = 0x08
ATTRIBUTE_LONG_NAME = 0x0F
DELETED = 0xE5
LOWER_STEM = 0x08
LOWER_EXTENSION = 0x10
END_OF_CHAIN = 0x0FFFFFF8
LONG_OFFSETS = (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)
CHAIN_LIMIT = 1 << 20


class FatReader:
    def __init__(self, disk, start):
        self.disk = disk
        self.start = start
        boot = disk.read(start, 1)
        self.sectors_per_cluster = boot[13]
        reserved, fats = struct.unpack_from("<HB", boot, 14)
        fat_sectors, = struct.unpack_from("<I", boot, 36)
        self.root, = struct.unpack_from("<I", boot, 44)
        self.fat_start = start + reserved
        self.data_start = self.fat_start + fats * fat_sectors

    def next_cluster(self, cluster):
        sector = self.disk.read(self.fat_start + cluster // 128, 1)
        return struct.unpack_from("<I", sector, cluster % 128 * 4)[0] & 0x0FFFFFFF

    def chain(self, first):
        clusters = []
        cluster = first
        while 2 <= cluster < END_OF_CHAIN and len(clusters) < CHAIN_LIMIT:
            clusters.append(cluster)
            cluster = self.next_cluster(cluster)
        return clusters

    def cluster_bytes(self, first):
        return b"".join(
            self.disk.read(self.data_start + (cluster - 2) * self.sectors_per_cluster,
                           self.sectors_per_cluster)
            for cluster in self.chain(first))

    def short_name(self, entry):
        stem = entry[0:8].decode("ascii", "replace").rstrip()
        extension = entry[8:11].decode("ascii", "replace").rstrip()
        if entry[12] & LOWER_STEM:
            stem = stem.lower()
        if entry[12] & LOWER_EXTENSION:
            extension = extension.lower()
        return stem + "." + extension if extension else stem

    def long_piece(self, entry):
        units = [struct.unpack_from("<H", entry, offset)[0] for offset in LONG_OFFSETS]
        return "".join(chr(unit) for unit in units if unit not in (0, 0xFFFF))

    def entries(self, first):
        """name -> (is a folder, first cluster, size) for one folder."""
        data = self.cluster_bytes(first)
        listed = {}
        long_name = ""
        for offset in range(0, len(data), ENTRY_BYTES):
            entry = data[offset:offset + ENTRY_BYTES]
            if entry[0] == 0:
                break
            if entry[0] == DELETED:
                long_name = ""
                continue
            if entry[11] == ATTRIBUTE_LONG_NAME:
                long_name = self.long_piece(entry) + long_name
                continue
            name = long_name or self.short_name(entry)
            long_name = ""
            if entry[11] & ATTRIBUTE_VOLUME or name in (".", ".."):
                continue
            high, = struct.unpack_from("<H", entry, 20)
            low, size = struct.unpack_from("<HI", entry, 26)
            listed[name] = (bool(entry[11] & ATTRIBUTE_DIRECTORY), high << 16 | low, size)
        return listed

    def find(self, path):
        found = (True, self.root, 0)
        for part in [part for part in path.split("/") if part]:
            if not found[0]:
                return None
            found = self.entries(found[1]).get(part)
            if found is None:
                return None
        return found

    def exists(self, path):
        return self.find(path) is not None

    def list(self, path):
        found = self.find(path)
        return sorted(self.entries(found[1])) if found and found[0] else []

    def read(self, path):
        found = self.find(path)
        if found is None or found[0]:
            return None
        return self.cluster_bytes(found[1])[:found[2]] if found[2] else b""

    def extract(self, path, target):
        """Copy a folder of the volume into a host folder, files and subfolders."""
        target = Path(target)
        target.mkdir(parents=True, exist_ok=True)
        found = self.find(path)
        if found is None or not found[0]:
            return target
        for name, (is_folder, _, _) in self.entries(found[1]).items():
            if is_folder:
                self.extract(path.rstrip("/") + "/" + name, target / name)
            else:
                (target / name).write_bytes(self.read(path.rstrip("/") + "/" + name))
        return target
