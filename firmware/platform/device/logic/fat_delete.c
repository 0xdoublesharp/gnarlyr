#include "device/fat.h"

#include "fat_create.h"
#include "fat_layout.h"

#define FAT32_ENTRY_MASK 0x0FFFFFFFu

enum {
    FIRST_CLUSTER = 2,
    ENTRY_SIZE = 32,
    ENTRY_DELETED = 0xE5,
    ENTRY_IN_USE_MASK = 0x7F,
    FAT16_PER_SECTOR = FAT_SECTOR_SIZE / 2,
    FAT32_PER_SECTOR = FAT_SECTOR_SIZE / 4
};

static bool valid_cluster(const FatVolume *volume, uint32_t cluster)
{
    return cluster >= FIRST_CLUSTER && cluster < volume->cluster_count + FIRST_CLUSTER;
}

static uint32_t per_sector(const FatVolume *volume)
{
    return volume->type == FAT_TYPE_16 ? FAT16_PER_SECTOR : FAT32_PER_SECTOR;
}

static bool split_path(const char *path, char *parent, const char **name, size_t *length)
{
    size_t last = 0;
    size_t total = 0;
    for (; path[total] != '\0'; total++) {
        last = path[total] == '/' ? total : last;
    }
    *name = path + last + 1;
    *length = total - last - 1;
    if (total == 0 || last >= FAT_PARENT_CAPACITY || *length == 0) {
        return false;
    }
    for (size_t index = 0; index < last; index++) {
        parent[index] = path[index];
    }
    parent[last] = '\0';
    return true;
}

static bool locate(FatVolume *volume, const char *path, FatFile *directory, FatEntryLocation *where)
{
    char parent[FAT_PARENT_CAPACITY];
    const char *name = NULL;
    size_t length = 0;
    FatDirCursor cursor;
    if (!split_path(path, parent, &name, &length) || !fat_open_directory(volume, parent, &cursor)) {
        return false;
    }
    *directory = cursor.directory;
    return fat_locate_entry(volume, directory, name, length, where) && !where->directory;
}

static bool mark_entry(FatVolume *volume, FatFile *directory, uint32_t index, bool exfat)
{
    const uint8_t *entry = fat_read_entry(volume, directory, index);
    if (entry == NULL) {
        return false;
    }
    uint8_t marker = exfat ? (uint8_t)(entry[0] & ENTRY_IN_USE_MASK) : ENTRY_DELETED;
    uint32_t offset = index * ENTRY_SIZE;
    uint32_t lba = 0;
    return fat_file_sector(volume, directory, offset / FAT_SECTOR_SIZE, &lba) &&
           fat_update_bytes(volume, lba, offset % FAT_SECTOR_SIZE, &marker, 1);
}

static bool mark_entries(FatVolume *volume, FatFile *directory, const FatEntryLocation *where)
{
    bool exfat = volume->type == FAT_TYPE_EXFAT;
    for (uint32_t step = 0; step < where->count; step++) {
        uint32_t index = exfat ? where->index + step : where->index + where->count - 1 - step;
        if (!mark_entry(volume, directory, index, exfat)) {
            return false;
        }
    }
    return true;
}

static uint32_t take_entry(const FatVolume *volume, uint8_t *sector, uint32_t within)
{
    if (volume->type == FAT_TYPE_16) {
        uint32_t value = fat_le16(sector + within * 2);
        sector[within * 2] = 0;
        sector[within * 2 + 1] = 0;
        return value;
    }
    uint8_t *entry = sector + within * 4;
    uint32_t value = fat_le32(entry) & FAT32_ENTRY_MASK;
    entry[0] = 0;
    entry[1] = 0;
    entry[2] = 0;
    entry[3] &= 0xF0;
    return value;
}

static bool write_copies(FatVolume *volume, uint32_t sector_index)
{
    uint32_t lba = volume->fat_start + sector_index;
    if (!fat_commit_sector(volume, lba)) {
        return false;
    }
    for (uint32_t copy = 1; copy < volume->fat_count; copy++) {
        if (!volume->device.write(volume->device.context, lba + copy * volume->fat_sectors, 1,
                                  fat_cached_sector(volume))) {
            return false;
        }
    }
    return true;
}

static bool clear_run(FatVolume *volume, uint32_t *cluster, uint32_t *freed)
{
    uint32_t per = per_sector(volume);
    uint32_t sector_index = *cluster / per;
    uint8_t *sector = fat_sector_for_update(volume, volume->fat_start + sector_index);
    if (sector == NULL) {
        return false;
    }
    do {
        *cluster = take_entry(volume, sector, *cluster % per);
        (*freed)++;
    } while (valid_cluster(volume, *cluster) && *cluster / per == sector_index);
    return write_copies(volume, sector_index);
}

static bool release_fat_chain(FatVolume *volume, uint32_t first)
{
    uint32_t cluster = first;
    uint32_t freed = 0;
    while (valid_cluster(volume, cluster)) {
        if (!clear_run(volume, &cluster, &freed)) {
            return false;
        }
    }
    volume->fat_sector_valid = false;
    if (volume->allocation_hint > first) {
        volume->allocation_hint = first;
    }
    return fat_info_record(volume, first, freed, true);
}

static bool release_exfat_runs(FatVolume *volume, FatFile *bitmap, const FatEntryLocation *where,
                               uint32_t clusters)
{
    uint32_t cluster = where->first_cluster;
    uint32_t run_first = cluster;
    uint32_t run_length = 0;
    for (uint32_t index = 0; index < clusters; index++) {
        if (!valid_cluster(volume, cluster)) {
            return false;
        }
        if (run_length > 0 && cluster != run_first + run_length) {
            if (!exfat_mark_clusters(volume, bitmap, run_first, run_length, false)) {
                return false;
            }
            run_length = 0;
        }
        run_first = run_length == 0 ? cluster : run_first;
        run_length++;
        if (index + 1 < clusters && !fat_next_cluster(volume, cluster, &cluster)) {
            return false;
        }
    }
    return exfat_mark_clusters(volume, bitmap, run_first, run_length, false);
}

static bool release_exfat(FatVolume *volume, const FatEntryLocation *where)
{
    FatFile bitmap;
    uint32_t cluster_bytes = (uint32_t)FAT_SECTOR_SIZE << volume->cluster_shift;
    uint32_t clusters = where->length == 0 ? 0 : (where->length - 1) / cluster_bytes + 1;
    if (clusters == 0) {
        return true;
    }
    if (!exfat_find_bitmap(volume, &bitmap)) {
        return false;
    }
    if (where->contiguous) {
        return exfat_mark_clusters(volume, &bitmap, where->first_cluster, clusters, false);
    }
    return release_exfat_runs(volume, &bitmap, where, clusters);
}

static bool release_clusters(FatVolume *volume, const FatEntryLocation *where)
{
    if (volume->type == FAT_TYPE_EXFAT) {
        return release_exfat(volume, where);
    }
    return where->first_cluster < FIRST_CLUSTER || release_fat_chain(volume, where->first_cluster);
}

bool fat_delete(FatVolume *volume, const char *path)
{
    FatFile directory;
    FatEntryLocation where;
    if (volume->device.write == NULL || !locate(volume, path, &directory, &where)) {
        return false;
    }
    volume->parent_valid = false;
    return mark_entries(volume, &directory, &where) && release_clusters(volume, &where);
}
