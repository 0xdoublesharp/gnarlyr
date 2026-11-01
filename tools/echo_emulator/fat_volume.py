import struct

SECTOR = 512
RESERVED_SECTORS = 32
FAT_COUNT = 2
SECTORS_PER_CLUSTER = 8
CLUSTER_BYTES = SECTORS_PER_CLUSTER * SECTOR
END_OF_CHAIN = 0x0FFFFFFF
ATTRIBUTE_ARCHIVE = 0x20


def short_name(name):
    stem, _, extension = name.partition(".")
    return stem.upper().ljust(8).encode("ascii") + extension.upper().ljust(3).encode("ascii")


class Fat32Volume:
    def __init__(self, megabytes=300):
        self.sector_count = megabytes * 2048
        clusters = (self.sector_count - RESERVED_SECTORS) // SECTORS_PER_CLUSTER
        self.fat_sectors = (clusters + 2 + 127) // 128
        self.data_start = RESERVED_SECTORS + FAT_COUNT * self.fat_sectors
        self.cluster_count = (self.sector_count - self.data_start) // SECTORS_PER_CLUSTER
        self.files = []

    def add_file(self, name, data=None, size=None):
        length = len(data) if data is not None else size
        self.files.append((name, data, length))

    def boot_sector(self):
        boot = bytearray(SECTOR)
        boot[0:3] = b"\xEB\x58\x90"
        boot[3:11] = b"MSDOS5.0"
        struct.pack_into("<HBHBHHBHHHII", boot, 11, SECTOR, SECTORS_PER_CLUSTER, RESERVED_SECTORS,
                         FAT_COUNT, 0, 0, 0xF8, 0, 32, 255, 0, self.sector_count)
        struct.pack_into("<IHHI", boot, 36, self.fat_sectors, 0, 0, 2)
        struct.pack_into("<HH", boot, 48, 0, 6)
        boot[64] = 0x80
        boot[66] = 0x29
        struct.pack_into("<I", boot, 67, 0x12345678)
        boot[71:82] = b"PODIUMTEST "
        boot[82:90] = b"FAT32   "
        struct.pack_into("<H", boot, 510, 0xAA55)
        return bytes(boot)

    def layout_files(self):
        fat = {0: 0x0FFFFFF8, 1: END_OF_CHAIN, 2: END_OF_CHAIN}
        entries = []
        extents = []
        next_cluster = 3
        for name, data, length in self.files:
            clusters = max(1, (length + CLUSTER_BYTES - 1) // CLUSTER_BYTES) if length else 0
            first = next_cluster if clusters else 0
            for index in range(clusters):
                fat[next_cluster + index] = (
                    next_cluster + index + 1 if index + 1 < clusters else END_OF_CHAIN)
            if clusters and data is not None:
                extents.append((first, data))
            entry = bytearray(32)
            entry[0:11] = short_name(name)
            entry[11] = ATTRIBUTE_ARCHIVE
            struct.pack_into("<H", entry, 20, first >> 16)
            struct.pack_into("<H", entry, 26, first & 0xFFFF)
            struct.pack_into("<I", entry, 28, length)
            entries.append(bytes(entry))
            next_cluster += clusters
        return fat, entries, extents

    def first_cluster(self, name):
        next_cluster = 3
        for candidate, _, length in self.files:
            clusters = max(1, (length + CLUSTER_BYTES - 1) // CLUSTER_BYTES) if length else 0
            if candidate == name:
                return next_cluster
            next_cluster += clusters
        raise KeyError(name)

    def cluster_lba(self, cluster):
        return self.data_start + (cluster - 2) * SECTORS_PER_CLUSTER

    def write_to(self, disk, start):
        fat, entries, extents = self.layout_files()
        disk.write(start, self.boot_sector())
        table = bytearray(self.fat_sectors * SECTOR)
        for cluster, value in fat.items():
            struct.pack_into("<I", table, 4 * cluster, value)
        for copy in range(FAT_COUNT):
            disk.write(start + RESERVED_SECTORS + copy * self.fat_sectors, bytes(table))
        root = b"".join(entries).ljust(CLUSTER_BYTES, b"\x00")
        disk.write(start + self.cluster_lba(2), root)
        for first, data in extents:
            padded = data + bytes(-len(data) % SECTOR)
            disk.write(start + self.cluster_lba(first), padded)
