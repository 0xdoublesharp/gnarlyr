#include "fat_layout.h"

#include "fat_extents.h"

#include <stddef.h>

#define FAT32_CLUSTER_MASK 0x0FFFFFFFu

enum {
    BOOT_SIGNATURE = 0xAA55,
    SIGNATURE_OFFSET = 510,
    PARTITION_ENTRY = 0x1BE,
    FIRST_CLUSTER = 2,
    FAT16_MINIMUM_CLUSTERS = 4085,
    FAT32_MINIMUM_CLUSTERS = 65525,
    DIRECTORY_ENTRY_SIZE = 32,
    EXFAT_SECTOR_SHIFT = 9,
    EXFAT_MAXIMUM_CLUSTER_SHIFT = 16,
    INFO_STRUCT_OFFSET = 484,
    INFO_FREE_OFFSET = 488,
    INFO_NEXT_OFFSET = 492
};

#define INFO_UNKNOWN 0xFFFFFFFFu
#define INFO_LEAD_SIGNATURE 0x41615252u
#define INFO_STRUCT_SIGNATURE 0x61417272u

uint16_t fat_le16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | bytes[1] << 8);
}

uint32_t fat_le32(const uint8_t *bytes)
{
    return (uint32_t)fat_le16(bytes) | (uint32_t)fat_le16(bytes + 2) << 16;
}

const uint8_t *fat_cached_sector(const FatVolume *volume)
{
    return (const uint8_t *)volume->cache + volume->cache_index * FAT_SECTOR_SIZE;
}

bool fat_sector_cached(FatVolume *volume, uint32_t lba)
{
    if (lba < volume->cache_lba || lba - volume->cache_lba >= volume->cache_count) {
        return false;
    }
    volume->cache_index = lba - volume->cache_lba;
    return true;
}

bool fat_read_run(FatVolume *volume, uint32_t lba, uint32_t count)
{
    if (fat_sector_cached(volume, lba)) {
        return true;
    }
    count = count < FAT_CACHE_SECTORS ? count : FAT_CACHE_SECTORS;
    volume->cache_lba = lba;
    volume->cache_index = 0;
    volume->cache_count = 0;
    if (!volume->device.read(volume->device.context, lba, count, volume->cache)) {
        return false;
    }
    volume->cache_count = count;
    return true;
}

bool fat_read_sector(FatVolume *volume, uint32_t lba)
{
    return fat_read_run(volume, lba, 1);
}

uint8_t *fat_sector_for_update(FatVolume *volume, uint32_t lba)
{
    if (!fat_read_run(volume, lba, 1)) {
        return NULL;
    }
    return (uint8_t *)volume->cache + volume->cache_index * FAT_SECTOR_SIZE;
}

bool fat_commit_sector(FatVolume *volume, uint32_t lba)
{
    if (volume->fat_sector_lba == lba) {
        volume->fat_sector_valid = false;
    }
    if (!fat_sector_cached(volume, lba) ||
        !volume->device.write(volume->device.context, lba, 1, fat_cached_sector(volume))) {
        volume->cache_count = 0;
        return false;
    }
    return true;
}

bool fat_write_run(FatVolume *volume, uint32_t lba, const uint8_t *bytes, uint32_t count)
{
    uint8_t *cache = (uint8_t *)volume->cache;
    uint32_t total = count * FAT_SECTOR_SIZE;
    for (uint32_t index = 0; index < total; index++) {
        cache[index] = bytes[index];
    }
    volume->cache_lba = lba;
    volume->cache_index = 0;
    volume->cache_count = count;
    if (!volume->device.write(volume->device.context, lba, count, volume->cache)) {
        volume->cache_count = 0;
        return false;
    }
    return true;
}

bool fat_update_bytes(FatVolume *volume, uint32_t lba, uint32_t within, const uint8_t *bytes,
                      uint32_t length)
{
    uint8_t *sector = fat_sector_for_update(volume, lba);
    if (sector == NULL) {
        return false;
    }
    for (uint32_t index = 0; index < length; index++) {
        sector[within + index] = bytes[index];
    }
    return fat_commit_sector(volume, lba);
}

bool fat_clear_sectors(FatVolume *volume, uint32_t lba, uint32_t count)
{
    volume->cache_count = 0;
    volume->fat_sector_valid = false;
    uint8_t *zero = (uint8_t *)volume->cache;
    for (uint32_t index = 0; index < FAT_SECTOR_SIZE; index++) {
        zero[index] = 0;
    }
    for (uint32_t index = 0; index < count; index++) {
        if (!volume->device.write(volume->device.context, lba + index, 1, zero)) {
            return false;
        }
    }
    return true;
}

uint32_t fat_cluster_lba(const FatVolume *volume, uint32_t cluster)
{
    return volume->data_start + ((cluster - FIRST_CLUSTER) << volume->cluster_shift);
}

static bool is_exfat_boot_sector(const uint8_t *sector)
{
    static const char NAME[] = "EXFAT   ";
    for (int index = 0; index < 8; index++) {
        if (sector[3 + index] != (uint8_t)NAME[index]) {
            return false;
        }
    }
    return fat_le16(sector + SIGNATURE_OFFSET) == BOOT_SIGNATURE;
}

static bool is_boot_sector(const uint8_t *sector)
{
    uint8_t per_cluster = sector[13];
    return fat_le16(sector + SIGNATURE_OFFSET) == BOOT_SIGNATURE &&
           (sector[0] == 0xEB || sector[0] == 0xE9) && fat_le16(sector + 11) == FAT_SECTOR_SIZE &&
           per_cluster != 0 && (per_cluster & (per_cluster - 1)) == 0 && sector[16] != 0;
}

static bool partition_start(const uint8_t *sector, uint32_t *start)
{
    const uint8_t *entry = sector + PARTITION_ENTRY;
    if (fat_le16(sector + SIGNATURE_OFFSET) != BOOT_SIGNATURE ||
        (entry[0] != 0x00 && entry[0] != 0x80) || entry[4] == 0) {
        return false;
    }
    *start = fat_le32(entry + 8);
    return *start != 0;
}

static uint32_t log2_of(uint32_t value)
{
    uint32_t shift = 0;
    while ((1u << shift) < value) {
        shift++;
    }
    return shift;
}

static bool read_layout(FatVolume *volume, const uint8_t *boot)
{
    uint32_t total = fat_le16(boot + 19) != 0 ? fat_le16(boot + 19) : fat_le32(boot + 32);
    uint32_t fat_size = fat_le16(boot + 22) != 0 ? fat_le16(boot + 22) : fat_le32(boot + 36);
    uint32_t root_entries = fat_le16(boot + 17);
    volume->root_sectors =
        (root_entries * DIRECTORY_ENTRY_SIZE + FAT_SECTOR_SIZE - 1) / FAT_SECTOR_SIZE;
    volume->fat_start = volume->volume_start + fat_le16(boot + 14);
    volume->fat_sectors = fat_size;
    volume->fat_count = boot[16];
    volume->root_start = volume->fat_start + boot[16] * fat_size;
    volume->data_start = volume->root_start + volume->root_sectors;
    volume->cluster_shift = log2_of(boot[13]);
    uint32_t used = volume->data_start - volume->volume_start;
    if (total <= used) {
        return false;
    }
    volume->cluster_count = (total - used) >> volume->cluster_shift;
    volume->root_cluster = fat_le32(boot + 44);
    volume->type = volume->cluster_count < FAT32_MINIMUM_CLUSTERS ? FAT_TYPE_16 : FAT_TYPE_32;
    uint32_t info = fat_le16(boot + 48);
    bool has_info = volume->type == FAT_TYPE_32 && info != 0 && info != 0xFFFF;
    volume->info_sector = has_info ? volume->volume_start + info : 0;
    return volume->cluster_count >= FAT16_MINIMUM_CLUSTERS &&
           (volume->type == FAT_TYPE_32 || root_entries != 0);
}

static bool read_exfat_layout(FatVolume *volume, const uint8_t *boot)
{
    volume->type = FAT_TYPE_EXFAT;
    volume->fat_start = volume->volume_start + fat_le32(boot + 80);
    volume->fat_sectors = fat_le32(boot + 84);
    volume->fat_count = boot[110];
    volume->data_start = volume->volume_start + fat_le32(boot + 88);
    volume->cluster_count = fat_le32(boot + 92);
    volume->root_cluster = fat_le32(boot + 96);
    volume->cluster_shift = boot[109];
    return boot[108] == EXFAT_SECTOR_SHIFT &&
           volume->cluster_shift <= EXFAT_MAXIMUM_CLUSTER_SHIFT && volume->cluster_count >= 1 &&
           volume->root_cluster >= FIRST_CLUSTER;
}

static bool is_volume(const uint8_t *sector)
{
    return is_boot_sector(sector) || is_exfat_boot_sector(sector);
}

static bool read_any_layout(FatVolume *volume)
{
    const uint8_t *boot = fat_cached_sector(volume);
    return is_exfat_boot_sector(boot) ? read_exfat_layout(volume, boot) : read_layout(volume, boot);
}

bool fat_mount(FatVolume *volume, BlockDevice device)
{
    *volume = (FatVolume){.device = device};
    uint32_t start = 0;
    if (!fat_read_sector(volume, 0)) {
        return false;
    }
    if (!is_volume(fat_cached_sector(volume))) {
        if (!partition_start(fat_cached_sector(volume), &start) ||
            !fat_read_sector(volume, start) || !is_volume(fat_cached_sector(volume))) {
            return false;
        }
    }
    volume->volume_start = start;
    return read_any_layout(volume);
}

static bool read_fat_sector(FatVolume *volume, uint32_t lba)
{
    if (volume->fat_sector_valid && volume->fat_sector_lba == lba) {
        return true;
    }
    volume->fat_sector_lba = lba;
    volume->fat_sector_valid =
        volume->device.read(volume->device.context, lba, 1, volume->fat_sector);
    return volume->fat_sector_valid;
}

static uint32_t chain_entry(const FatVolume *volume, const uint8_t *bytes)
{
    if (volume->type == FAT_TYPE_16) {
        return fat_le16(bytes);
    }
    return fat_le32(bytes) & (volume->type == FAT_TYPE_32 ? FAT32_CLUSTER_MASK : UINT32_MAX);
}

static bool chain_valid(const FatVolume *volume, uint32_t next)
{
    return next >= FIRST_CLUSTER && next < volume->cluster_count + FIRST_CLUSTER;
}

static uint32_t chain_offset(const FatVolume *volume, uint32_t cluster)
{
    return cluster * (volume->type == FAT_TYPE_16 ? 2u : 4u);
}

bool fat_next_cluster(FatVolume *volume, uint32_t cluster, uint32_t *next)
{
    uint32_t offset = chain_offset(volume, cluster);
    if (!read_fat_sector(volume, volume->fat_start + offset / FAT_SECTOR_SIZE)) {
        return false;
    }
    *next = chain_entry(volume, (const uint8_t *)volume->fat_sector + offset % FAT_SECTOR_SIZE);
    return chain_valid(volume, *next);
}

bool fat_chain_next(FatVolume *volume, uint32_t cluster, uint32_t *next)
{
    uint32_t offset = chain_offset(volume, cluster);
    uint32_t lba = volume->fat_start + offset / FAT_SECTOR_SIZE;
    uint32_t left = volume->fat_start + volume->fat_sectors - lba;
    if (left == 0 ||
        !fat_read_run(volume, lba, left < FAT_CACHE_SECTORS ? left : FAT_CACHE_SECTORS)) {
        return false;
    }
    *next = chain_entry(volume, fat_cached_sector(volume) + offset % FAT_SECTOR_SIZE);
    return chain_valid(volume, *next);
}

static bool walk_to(FatVolume *volume, FatFile *file, uint32_t index, uint32_t *cluster)
{
    if (file->cursor_cluster == 0 || index < file->cursor_index) {
        file->cursor_index = 0;
        file->cursor_cluster = file->first_cluster;
    }
    while (file->cursor_index < index) {
        if (!fat_next_cluster(volume, file->cursor_cluster, &file->cursor_cluster)) {
            file->cursor_cluster = 0;
            return false;
        }
        file->cursor_index++;
    }
    *cluster = file->cursor_cluster;
    return *cluster >= FIRST_CLUSTER;
}

static bool cluster_at(FatVolume *volume, FatFile *file, uint32_t index, uint32_t *cluster)
{
    if (index >= volume->cluster_count) {
        return false;
    }
    if (file->contiguous) {
        *cluster = file->first_cluster + index;
        return file->first_cluster >= FIRST_CLUSTER &&
               *cluster < volume->cluster_count + FIRST_CLUSTER;
    }
    FatLookup lookup = fat_extents_lookup(volume, file, index, cluster);
    if (lookup != FAT_LOOKUP_UNMAPPED) {
        return lookup == FAT_LOOKUP_FOUND;
    }
    return walk_to(volume, file, index, cluster);
}

static bool is_fixed_root(const FatFile *file)
{
    return file->directory && file->first_cluster == 0;
}

bool fat_file_sector(FatVolume *volume, FatFile *file, uint32_t sector_index, uint32_t *lba)
{
    if (is_fixed_root(file)) {
        *lba = volume->root_start + sector_index;
        return sector_index < volume->root_sectors;
    }
    uint32_t cluster = 0;
    if (!cluster_at(volume, file, sector_index >> volume->cluster_shift, &cluster)) {
        return false;
    }
    uint32_t within = sector_index & ((1u << volume->cluster_shift) - 1u);
    *lba = volume->data_start + ((cluster - FIRST_CLUSTER) << volume->cluster_shift) + within;
    return true;
}

FatFile fat_root_directory(const FatVolume *volume)
{
    uint32_t first = volume->type == FAT_TYPE_16 ? 0 : volume->root_cluster;
    return (FatFile){.first_cluster = first, .size = UINT32_MAX, .directory = true};
}

uint32_t fat_run_length(FatVolume *volume, FatFile *file, uint32_t sector_index, uint32_t limit)
{
    FatFile ahead = *file;
    uint32_t first = 0;
    if (!fat_file_sector(volume, &ahead, sector_index, &first)) {
        return 0;
    }
    uint32_t length = 1;
    uint32_t lba = first;
    while (length < limit && fat_file_sector(volume, &ahead, sector_index + length, &lba) &&
           lba == first + length) {
        length++;
    }
    return length;
}

const uint8_t *fat_read_entry(FatVolume *volume, FatFile *directory, uint32_t index)
{
    uint32_t offset = index * DIRECTORY_ENTRY_SIZE;
    uint32_t sector = offset / FAT_SECTOR_SIZE;
    uint32_t lba = 0;
    if (offset >= directory->size || !fat_file_sector(volume, directory, sector, &lba)) {
        return NULL;
    }
    if (!fat_sector_cached(volume, lba)) {
        uint32_t left = (directory->size - 1) / FAT_SECTOR_SIZE - sector + 1;
        uint32_t run = fat_run_length(volume, directory, sector,
                                      left < FAT_CACHE_SECTORS ? left : FAT_CACHE_SECTORS);
        if (run == 0 || !fat_read_run(volume, lba, run)) {
            return NULL;
        }
    }
    return fat_cached_sector(volume) + offset % FAT_SECTOR_SIZE;
}

static bool info_valid(const uint8_t *sector)
{
    return fat_le32(sector) == INFO_LEAD_SIGNATURE &&
           fat_le32(sector + INFO_STRUCT_OFFSET) == INFO_STRUCT_SIGNATURE;
}

static void put32(uint8_t *bytes, uint32_t value)
{
    for (int index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

uint32_t fat_info_next_free(FatVolume *volume)
{
    if (volume->info_sector == 0 || !fat_read_sector(volume, volume->info_sector) ||
        !info_valid(fat_cached_sector(volume))) {
        return FIRST_CLUSTER;
    }
    uint32_t next = fat_le32(fat_cached_sector(volume) + INFO_NEXT_OFFSET);
    return next >= FIRST_CLUSTER && next < volume->cluster_count + FIRST_CLUSTER ? next
                                                                                 : FIRST_CLUSTER;
}

static uint32_t adjusted_free_count(uint32_t free_count, uint32_t count, bool released)
{
    if (free_count == INFO_UNKNOWN) {
        return free_count;
    }
    if (released) {
        return free_count + count;
    }
    return free_count >= count ? free_count - count : 0;
}

bool fat_info_record(FatVolume *volume, uint32_t next_free, uint32_t count, bool released)
{
    if (volume->info_sector == 0) {
        return true;
    }
    uint8_t *sector = fat_sector_for_update(volume, volume->info_sector);
    if (sector == NULL) {
        return false;
    }
    if (!info_valid(sector)) {
        return true;
    }
    uint32_t free_count = fat_le32(sector + INFO_FREE_OFFSET);
    put32(sector + INFO_FREE_OFFSET, adjusted_free_count(free_count, count, released));
    put32(sector + INFO_NEXT_OFFSET, next_free);
    return fat_commit_sector(volume, volume->info_sector);
}

static uint8_t label_fold(uint8_t value)
{
    return value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value;
}

static bool label_matches(const uint8_t *entry, const char *name, bool exfat)
{
    unsigned length = 0;
    while (name[length] != 0) {
        length++;
    }
    if (exfat && entry[1] != length) {
        return false;
    }
    for (unsigned index = 0; index < 11; index++) {
        uint8_t expected = index < length ? (uint8_t)name[index] : ' ';
        if (exfat && index >= length) {
            break;
        }
        unsigned offset = exfat ? 2 + index * 2 : index;
        if (label_fold(entry[offset]) != label_fold(expected) ||
            (exfat && entry[offset + 1] != 0)) {
            return false;
        }
    }
    return true;
}

bool fat_fix_label(FatVolume *volume, const char *target, const char *legacy)
{
    FatFile root = fat_root_directory(volume);
    bool exfat = volume->type == FAT_TYPE_EXFAT;
    for (uint32_t index = 0; index < 65536; index++) {
        const uint8_t *entry = fat_read_entry(volume, &root, index);
        if (entry == NULL || entry[0] == 0) {
            return true;
        }
        if (exfat ? entry[0] != 0x83 : entry[0] == 0xE5 || entry[11] != 8) {
            continue;
        }
        if (!label_matches(entry, target, exfat) &&
            (legacy == NULL || !label_matches(entry, legacy, exfat))) {
            return true;
        }
        uint8_t desired[32];
        for (unsigned byte = 0; byte < 32; byte++) {
            desired[byte] = entry[byte];
        }
        unsigned length = 0;
        while (target[length] != 0 && length < 11) {
            length++;
        }
        if (exfat) {
            desired[1] = (uint8_t)length;
        }
        bool changed = false;
        for (unsigned byte = 0; byte < 11; byte++) {
            unsigned offset = exfat ? 2 + byte * 2 : byte;
            desired[offset] = byte < length ? (uint8_t)target[byte] : exfat ? 0 : ' ';
            if (exfat) {
                desired[offset + 1] = 0;
            }
        }
        for (unsigned byte = 0; byte < 32; byte++) {
            changed |= desired[byte] != entry[byte];
        }
        uint32_t lba;
        if (changed && (!fat_file_sector(volume, &root, index / 16, &lba) ||
                        !fat_update_bytes(volume, lba, index % 16 * 32, desired, 32))) {
            return false;
        }
        if (volume->type == FAT_TYPE_32) {
            if (!fat_read_sector(volume, volume->volume_start)) {
                return false;
            }
            const uint8_t *boot = fat_cached_sector(volume);
            uint8_t header[71];
            for (unsigned byte = 0; byte < sizeof header; byte++) {
                header[byte] = boot[byte];
            }
            uint32_t backup = fat_le16(boot + 50);
            uint32_t reserved = fat_le16(boot + 14);
            for (unsigned copy = 0; copy < 2; copy++) {
                if (copy && (backup == 0 || backup >= reserved)) {
                    break;
                }
                lba = volume->volume_start + (copy ? backup : 0);
                if (!fat_read_sector(volume, lba)) {
                    return false;
                }
                boot = fat_cached_sector(volume);
                if (fat_le16(boot + 510) != 0xAA55) {
                    return false;
                }
                for (unsigned byte = 0; byte < sizeof header; byte++) {
                    if (boot[byte] != header[byte]) {
                        return false;
                    }
                }
                changed = false;
                for (unsigned byte = 0; byte < 11; byte++) {
                    changed |= boot[71 + byte] != desired[byte];
                }
                if (changed && !fat_update_bytes(volume, lba, 71, desired, 11)) {
                    return false;
                }
            }
        }
        return true;
    }
    return false;
}
