import struct

SECTOR = 512
RESERVED_SECTORS = 32
FAT_COUNT = 2
END_OF_CHAIN = 0x0FFFFFFF
ATTRIBUTE_DIRECTORY = 0x10
ATTRIBUTE_ARCHIVE = 0x20
ATTRIBUTE_LONG_NAME = 0x0F
ENTRY_BYTES = 32
LONG_CHARACTERS = 13
LONG_OFFSETS = (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)
FIXED_DATE = (2024 - 1980) << 9 | 6 << 5 | 15
FIXED_TIME = 12 << 11
FSINFO_SECTOR = 1
BACKUP_BOOT_SECTOR = 6


def short_name(name):
    stem, _, extension = name.partition(".")
    return stem.upper().ljust(8).encode("ascii") + extension.upper().ljust(3).encode("ascii")


def fits_short_name(name):
    stem, _, extension = name.partition(".")
    plain = all(character.isascii() and character.isalnum() and not character.islower()
                or character in "_-" for character in stem + extension)
    return plain and 0 < len(stem) <= 8 and len(extension) <= 3 and "." not in extension


def checksum(raw):
    total = 0
    for byte in raw:
        total = (((total & 1) << 7) + (total >> 1) + byte) & 0xFF
    return total


def long_entries(name, raw_short):
    units = [ord(character) for character in name] + [0]
    count = (len(units) + LONG_CHARACTERS - 1) // LONG_CHARACTERS
    units += [0xFFFF] * (count * LONG_CHARACTERS - len(units))
    result = []
    for ordinal in range(count, 0, -1):
        entry = bytearray(ENTRY_BYTES)
        entry[0] = ordinal | (0x40 if ordinal == count else 0)
        entry[11] = ATTRIBUTE_LONG_NAME
        entry[13] = checksum(raw_short)
        chunk = units[(ordinal - 1) * LONG_CHARACTERS:ordinal * LONG_CHARACTERS]
        for offset, unit in zip(LONG_OFFSETS, chunk):
            struct.pack_into("<H", entry, offset, unit)
        result.append(bytes(entry))
    return result


def long_entry_total(name):
    return (len(name) + 1 + LONG_CHARACTERS - 1) // LONG_CHARACTERS


class Node:
    def __init__(self, name, directory, data=None, size=0, source=None):
        self.name = name
        self.source = source
        self.directory = directory
        self.data = data
        self.size = size or len(data or b"")
        self.children = []
        self.first = 0
        self.clusters = 0
        self.alias_counts = {}


class Fat32Volume:
    def __init__(self, megabytes=300, sectors_per_cluster=8, fsinfo=False, next_free=None):
        self.next_free = next_free
        self.sectors_per_cluster = sectors_per_cluster
        self.cluster_bytes = sectors_per_cluster * SECTOR
        self.fsinfo = fsinfo
        self.sector_count = megabytes * 2048
        clusters = (self.sector_count - RESERVED_SECTORS) // sectors_per_cluster
        self.fat_sectors = (clusters + 2 + 127) // 128
        self.data_start = RESERVED_SECTORS + FAT_COUNT * self.fat_sectors
        self.cluster_count = (self.sector_count - self.data_start) // sectors_per_cluster
        self.root = Node("", True)
        self.laid_out = False
        self.used_clusters = 0

    def directory(self, path):
        node = self.root
        for part in [part for part in path.split("/") if part]:
            node = next(child for child in node.children if child.name == part and child.directory)
        return node

    def add_file(self, name, data=None, size=None, directory="", source=None):
        self.directory(directory).children.append(
            Node(name, False, data, source.size if source else size or 0, source))

    def add_directory(self, name, directory=""):
        self.directory(directory).children.append(Node(name, True))

    @property
    def files(self):
        return [(child.name, child.data, child.size) for child in self.root.children
                if not child.directory]

    def cluster_span(self, length):
        return max(1, (length + self.cluster_bytes - 1) // self.cluster_bytes) if length else 0

    def entry_count(self, node):
        count = 0 if node is self.root else 2
        for child in node.children:
            count += 1 if fits_short_name(child.name) else long_entry_total(child.name) + 1
        return count

    def directory_span(self, node):
        entries_bytes = self.entry_count(node) * ENTRY_BYTES
        return max(1, (entries_bytes + self.cluster_bytes - 1) // self.cluster_bytes)

    def assign(self):
        next_cluster = 2
        stack = [self.root]
        while stack:
            node = stack.pop()
            node.clusters = self.directory_span(node)
            node.first = next_cluster
            next_cluster += node.clusters
            subdirectories = []
            for child in node.children:
                if child.directory:
                    subdirectories.append(child)
                else:
                    child.clusters = self.cluster_span(child.size)
                    child.first = next_cluster if child.clusters else 0
                    next_cluster += child.clusters
            stack.extend(reversed(subdirectories))
        self.used_clusters = next_cluster - 2
        if self.used_clusters > self.cluster_count:
            raise ValueError("volume too small")
        self.laid_out = True

    def boot_sector(self):
        boot = bytearray(SECTOR)
        boot[0:3] = b"\xEB\x58\x90"
        boot[3:11] = b"MSDOS5.0"
        struct.pack_into("<HBHBHHBHHHII", boot, 11, SECTOR, self.sectors_per_cluster,
                         RESERVED_SECTORS, FAT_COUNT, 0, 0, 0xF8, 0, 32, 255, 0, self.sector_count)
        struct.pack_into("<IHHI", boot, 36, self.fat_sectors, 0, 0, 2)
        info = FSINFO_SECTOR if self.fsinfo else 0
        struct.pack_into("<HH", boot, 48, info, BACKUP_BOOT_SECTOR)
        boot[64] = 0x80
        boot[66] = 0x29
        struct.pack_into("<I", boot, 67, 0x12345678)
        boot[71:82] = b"PODIUMTEST "
        boot[82:90] = b"FAT32   "
        struct.pack_into("<H", boot, 510, 0xAA55)
        return bytes(boot)

    def info_sector(self):
        sector = bytearray(SECTOR)
        struct.pack_into("<I", sector, 0, 0x41615252)
        struct.pack_into("<I", sector, 484, 0x61417272)
        struct.pack_into("<II", sector, 488, self.cluster_count - self.used_clusters,
                         2 + self.used_clusters if self.next_free is None else self.next_free)
        struct.pack_into("<I", sector, 508, 0xAA550000)
        return bytes(sector)

    def alias(self, parent, name):
        stem = "".join(character for character in name.upper()
                       if character.isascii() and character.isalnum())[:6] or "X"
        count = parent.alias_counts.get(stem, 0) + 1
        parent.alias_counts[stem] = count
        return (stem + "~" + str(count)).ljust(8).encode("ascii") + b"   "

    def entry_bytes(self, raw, attribute, first, length):
        entry = bytearray(ENTRY_BYTES)
        entry[0:11] = raw
        entry[11] = attribute
        struct.pack_into("<HHH", entry, 14, FIXED_TIME, FIXED_DATE, FIXED_DATE)
        struct.pack_into("<H", entry, 20, first >> 16)
        struct.pack_into("<HH", entry, 22, FIXED_TIME, FIXED_DATE)
        struct.pack_into("<H", entry, 26, first & 0xFFFF)
        struct.pack_into("<I", entry, 28, length)
        return bytes(entry)

    def directory_bytes(self, node, parent):
        entries = []
        if node is not self.root:
            up = 0 if parent is self.root else parent.first
            entries.append(self.entry_bytes(b".".ljust(11), ATTRIBUTE_DIRECTORY, node.first, 0))
            entries.append(self.entry_bytes(b"..".ljust(11), ATTRIBUTE_DIRECTORY, up, 0))
        for child in node.children:
            attribute = ATTRIBUTE_DIRECTORY if child.directory else ATTRIBUTE_ARCHIVE
            if fits_short_name(child.name):
                raw = short_name(child.name)
            else:
                raw = self.alias(node, child.name)
                entries.extend(long_entries(child.name, raw))
            entries.append(self.entry_bytes(raw, attribute, child.first,
                                            0 if child.directory else child.size))
        return b"".join(entries).ljust(node.clusters * self.cluster_bytes, b"\x00")

    def walk(self):
        stack = [(self.root, None)]
        while stack:
            node, parent = stack.pop()
            yield node, parent
            stack.extend((child, node) for child in node.children if child.directory)

    def fat_table(self):
        table = bytearray(self.fat_sectors * SECTOR)
        struct.pack_into("<II", table, 0, 0x0FFFFFF8, END_OF_CHAIN)
        for node, _ in self.walk():
            self.chain(table, node.first, node.clusters)
            for child in node.children:
                if not child.directory:
                    self.chain(table, child.first, child.clusters)
        return table

    def chain(self, table, first, count):
        if count == 0:
            return
        values = list(range(first + 1, first + count)) + [END_OF_CHAIN]
        struct.pack_into("<%dI" % count, table, 4 * first, *values)

    def first_cluster(self, name):
        if not self.laid_out:
            self.assign()
        return next(child.first for child in self.root.children if child.name == name)

    def cluster_lba(self, cluster):
        return self.data_start + (cluster - 2) * self.sectors_per_cluster

    def write_to(self, disk, start):
        if not self.laid_out:
            self.assign()
        disk.write(start, self.boot_sector())
        disk.write(start + BACKUP_BOOT_SECTOR, self.boot_sector())
        if self.fsinfo:
            disk.write(start + FSINFO_SECTOR, self.info_sector())
            disk.write(start + BACKUP_BOOT_SECTOR + 1, self.info_sector())
        table = bytes(self.fat_table())
        for copy in range(FAT_COUNT):
            disk.write(start + RESERVED_SECTORS + copy * self.fat_sectors, table)
        for node, parent in self.walk():
            disk.write(start + self.cluster_lba(node.first), self.directory_bytes(node, parent))
            for child in node.children:
                if child.directory or not child.clusters:
                    continue
                if child.source is not None:
                    disk.add_source(start + self.cluster_lba(child.first), child.size, child.source)
                elif child.data is not None:
                    padded = child.data + bytes(-len(child.data) % SECTOR)
                    disk.write(start + self.cluster_lba(child.first), padded)
