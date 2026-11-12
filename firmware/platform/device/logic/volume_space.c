#include "device/volume_space.h"

#include <stdbool.h>
#include <stddef.h>

enum {
    PARTITION_START = 446 + 8,
    SIGNATURE_AT = 510,
    FAT32_LABEL_AT = 82,
    FAT16_LABEL_AT = 54,
    DIRECTORY_ENTRY = 32,
    FAT32_ENTRY_MASK = 0x0FFFFFFF,
    EXFAT_LABEL_AT = 3,
    LABEL_BYTES = 8,
    INFO_LEAD = 0x41615252,
    INFO_STRUCT = 0x61417272,
    INFO_STRUCT_AT = 484,
    INFO_FREE_AT = 488,
    ENTRY_BYTES = 32,
    BITMAP_ENTRY = 0x81,
    END_ENTRY = 0x00,
    FIRST_CLUSTER = 2,
    BITS_PER_SECTOR = VOLUME_SPACE_SECTOR * 8,
    SECTOR_SHIFT = 9
};

typedef struct Reader {
    BlockDevice device;
    VolumeSpaceBuffer buffer;
    const uint8_t *bytes;
} Reader;

typedef struct Exfat {
    uint32_t start;
    uint32_t heap;
    uint32_t clusters;
    uint32_t root;
    uint8_t shift;
} Exfat;

static uint16_t le16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | bytes[1] << 8);
}

static uint32_t le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static bool same(const uint8_t *bytes, const char *text)
{
    for (int index = 0; index < LABEL_BYTES; index++) {
        if (bytes[index] != (uint8_t)text[index]) {
            return false;
        }
    }
    return true;
}

static bool read_run(Reader *reader, uint32_t lba, uint32_t count)
{
    return reader->device.read(reader->device.context, lba, count, reader->buffer.words);
}

static bool is_fat32(const uint8_t *bytes)
{
    return same(bytes + FAT32_LABEL_AT, "FAT32   ") && le16(bytes + 11) == VOLUME_SPACE_SECTOR &&
           bytes[13] != 0;
}

static bool is_fat16(const uint8_t *bytes)
{
    return same(bytes + FAT16_LABEL_AT, "FAT16   ") && le16(bytes + 11) == VOLUME_SPACE_SECTOR &&
           bytes[13] != 0;
}

static bool is_exfat(const uint8_t *bytes)
{
    return same(bytes + EXFAT_LABEL_AT, "EXFAT   ") && bytes[108] == SECTOR_SHIFT;
}

static bool signed_sector(const uint8_t *bytes)
{
    return bytes[SIGNATURE_AT] == 0x55 && bytes[SIGNATURE_AT + 1] == 0xAA;
}

static bool find_volume(Reader *reader, uint32_t *start)
{
    *start = 0;
    if (!read_run(reader, 0, 1) || !signed_sector(reader->bytes)) {
        return false;
    }
    if (is_fat32(reader->bytes) || is_exfat(reader->bytes) || is_fat16(reader->bytes)) {
        return true;
    }
    *start = le32(reader->bytes + PARTITION_START);
    return *start != 0 && read_run(reader, *start, 1) && signed_sector(reader->bytes);
}

static uint32_t sectors_of(uint32_t clusters, uint32_t shift)
{
    uint64_t sectors = (uint64_t)clusters << shift;
    return sectors < VOLUME_SPACE_UNKNOWN ? (uint32_t)sectors : VOLUME_SPACE_UNKNOWN - 1;
}

static uint32_t shift_of(uint32_t per_cluster)
{
    uint32_t shift = 0;
    while ((1u << shift) < per_cluster) {
        shift++;
    }
    return (1u << shift) == per_cluster ? shift : 0;
}

typedef struct FatTable {
    uint32_t fat;
    uint32_t fat_sectors;
    uint32_t clusters;
    uint32_t per_cluster;
    uint32_t entry_bytes;
} FatTable;

static uint32_t clusters_after(uint32_t total, uint32_t system, uint32_t per_cluster)
{
    return total > system ? (total - system) / per_cluster : 0;
}

static FatTable fat32_of(const uint8_t *bytes, uint32_t start)
{
    uint32_t fat_sectors = le32(bytes + 36);
    uint64_t system = (uint64_t)le16(bytes + 14) + (uint64_t)bytes[16] * fat_sectors;
    uint32_t clusters = system < le32(bytes + 32)
                            ? clusters_after(le32(bytes + 32), (uint32_t)system, bytes[13])
                            : 0;
    return (FatTable){start + le16(bytes + 14), fat_sectors, clusters, bytes[13], 4};
}

static FatTable fat16_of(const uint8_t *bytes, uint32_t start)
{
    uint32_t total = le16(bytes + 19) != 0 ? le16(bytes + 19) : le32(bytes + 32);
    uint32_t fat_sectors = le16(bytes + 22);
    uint32_t root = ((uint32_t)le16(bytes + 17) * DIRECTORY_ENTRY + VOLUME_SPACE_SECTOR - 1) /
                    VOLUME_SPACE_SECTOR;
    uint32_t system = le16(bytes + 14) + bytes[16] * fat_sectors + root;
    return (FatTable){start + le16(bytes + 14), fat_sectors,
                      clusters_after(total, system, bytes[13]), bytes[13], 2};
}

static bool table_entry_free(const uint8_t *bytes, uint32_t entry_bytes)
{
    return entry_bytes == 2 ? le16(bytes) == 0 : (le32(bytes) & FAT32_ENTRY_MASK) == 0;
}

static uint32_t table_free(Reader *reader, FatTable table)
{
    uint32_t entries = table.clusters + FIRST_CLUSTER;
    uint32_t per_sector = VOLUME_SPACE_SECTOR / table.entry_bytes;
    if (table.fat_sectors == 0 || table.fat_sectors > VOLUME_SPACE_BITMAP_SECTORS ||
        entries > table.fat_sectors * per_sector) {
        return VOLUME_SPACE_UNKNOWN;
    }
    uint32_t free = 0;
    uint32_t per_read = reader->buffer.sectors * per_sector;
    for (uint32_t first = 0; first < entries; first += per_read) {
        uint32_t count = entries - first < per_read ? entries - first : per_read;
        if (!read_run(reader, table.fat + first / per_sector,
                      (count + per_sector - 1) / per_sector)) {
            return VOLUME_SPACE_UNKNOWN;
        }
        for (uint32_t index = first < FIRST_CLUSTER ? FIRST_CLUSTER - first : 0; index < count;
             index++) {
            free += table_entry_free(reader->bytes + index * table.entry_bytes, table.entry_bytes);
        }
    }
    return sectors_of(free, shift_of(table.per_cluster));
}

static uint32_t fat32_free(Reader *reader, uint32_t start)
{
    FatTable table = fat32_of(reader->bytes, start);
    uint32_t info = le16(reader->bytes + 48);
    const uint8_t *bytes = reader->bytes;
    if (info != 0 && read_run(reader, start + info, 1) && le32(bytes) == INFO_LEAD &&
        le32(bytes + INFO_STRUCT_AT) == INFO_STRUCT &&
        le32(bytes + INFO_FREE_AT) <= table.clusters) {
        return sectors_of(le32(bytes + INFO_FREE_AT), shift_of(table.per_cluster));
    }
    return table_free(reader, table);
}

static uint32_t fat16_free(Reader *reader, uint32_t start)
{
    return table_free(reader, fat16_of(reader->bytes, start));
}

static uint32_t cluster_lba(const Exfat *volume, uint32_t cluster)
{
    return volume->start + volume->heap + ((cluster - FIRST_CLUSTER) << volume->shift);
}

static bool find_bitmap(Reader *reader, const Exfat *volume, uint32_t *first)
{
    if (!read_run(reader, cluster_lba(volume, volume->root), 1)) {
        return false;
    }
    for (uint32_t at = 0; at < VOLUME_SPACE_SECTOR; at += ENTRY_BYTES) {
        const uint8_t *entry = reader->bytes + at;
        if (entry[0] == END_ENTRY) {
            return false;
        }
        if (entry[0] == BITMAP_ENTRY && (entry[1] & 1u) == 0) {
            *first = le32(entry + 20);
            return *first >= FIRST_CLUSTER && le32(entry + 28) == 0 &&
                   le32(entry + 24) >= (volume->clusters + 7) / 8;
        }
    }
    return false;
}

static uint32_t ones(uint32_t word)
{
    word -= (word >> 1) & 0x55555555u;
    word = (word & 0x33333333u) + ((word >> 2) & 0x33333333u);
    word = (word + (word >> 4)) & 0x0F0F0F0Fu;
    return (word * 0x01010101u) >> 24;
}

static uint32_t count_used(const uint32_t *words, uint32_t bits)
{
    uint32_t used = 0;
    for (uint32_t index = 0; index < bits / 32; index++) {
        used += ones(words[index]);
    }
    const uint8_t *tail = (const uint8_t *)(words + bits / 32);
    uint32_t left = bits % 32;
    uint32_t last = 0;
    for (uint32_t index = 0; index < (left + 7) / 8; index++) {
        last |= (uint32_t)tail[index] << (8 * index);
    }
    return used + (left == 0 ? 0 : ones(last & ((1u << left) - 1u)));
}

static uint32_t bitmap_used(Reader *reader, const Exfat *volume, uint32_t lba)
{
    uint32_t used = 0;
    for (uint32_t done = 0; done < volume->clusters;) {
        uint32_t left = (volume->clusters - done + BITS_PER_SECTOR - 1) / BITS_PER_SECTOR;
        uint32_t count = left < reader->buffer.sectors ? left : reader->buffer.sectors;
        if (!read_run(reader, lba, count)) {
            return VOLUME_SPACE_UNKNOWN;
        }
        uint32_t bits = count * BITS_PER_SECTOR;
        bits = bits < volume->clusters - done ? bits : volume->clusters - done;
        used += count_used(reader->buffer.words, bits);
        done += bits;
        lba += count;
    }
    return used;
}

static uint32_t exfat_free(Reader *reader, uint32_t start)
{
    const uint8_t *bytes = reader->bytes;
    Exfat volume = {start, le32(bytes + 88), le32(bytes + 92), le32(bytes + 96), bytes[109]};
    uint32_t first = 0;
    uint32_t sectors = (volume.clusters + BITS_PER_SECTOR - 1) / BITS_PER_SECTOR;
    if (volume.shift > 25 || volume.root < FIRST_CLUSTER || sectors > VOLUME_SPACE_BITMAP_SECTORS ||
        !find_bitmap(reader, &volume, &first)) {
        return VOLUME_SPACE_UNKNOWN;
    }
    uint32_t used = bitmap_used(reader, &volume, cluster_lba(&volume, first));
    if (used > volume.clusters) {
        return VOLUME_SPACE_UNKNOWN;
    }
    return sectors_of(volume.clusters - used, volume.shift);
}

uint32_t volume_space_free_sectors(BlockDevice device, VolumeSpaceBuffer buffer)
{
    Reader reader = {device, buffer, (const uint8_t *)buffer.words};
    uint32_t start = 0;
    if (buffer.words == NULL || buffer.sectors == 0 || !find_volume(&reader, &start)) {
        return VOLUME_SPACE_UNKNOWN;
    }
    if (is_exfat(reader.bytes)) {
        return exfat_free(&reader, start);
    }
    if (is_fat16(reader.bytes)) {
        return fat16_free(&reader, start);
    }
    return is_fat32(reader.bytes) ? fat32_free(&reader, start) : VOLUME_SPACE_UNKNOWN;
}
