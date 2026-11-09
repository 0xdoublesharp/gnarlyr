#include "fat_extents.h"

#include <stddef.h>

#include "fat_layout.h"

enum {
    WALK_AHEAD_CLUSTERS = 128
};

typedef enum ScanResult {
    SCAN_DONE,
    SCAN_FULL,
    SCAN_BROKEN
} ScanResult;

void fat_attach_extents(FatFile *file, FatExtents *extents)
{
    *extents = (FatExtents){.state = FAT_EXTENTS_IDLE};
    file->extents = file->directory ? NULL : extents;
}

static uint32_t file_clusters(const FatVolume *volume, const FatFile *file)
{
    uint32_t bytes = (uint32_t)FAT_SECTOR_SIZE << volume->cluster_shift;
    return file->size / bytes + (file->size % bytes != 0 ? 1u : 0u);
}

static bool wanted(const FatExtents *map, uint32_t index, uint32_t cluster, uint32_t previous)
{
    if (map->stride != 0) {
        return index % map->stride == 0;
    }
    return index == 0 || cluster != previous + 1;
}

static bool add_entry(FatExtents *map, uint32_t index, uint32_t cluster)
{
    if (map->count == FAT_EXTENT_CAPACITY) {
        return false;
    }
    map->entry[map->count++] = (FatExtent){.index = index, .cluster = cluster};
    return true;
}

static ScanResult scan_chain(FatVolume *volume, const FatFile *file, FatExtents *map)
{
    uint32_t cluster = file->first_cluster;
    uint32_t previous = 0;
    for (uint32_t index = 0; index < map->clusters; index++) {
        if (wanted(map, index, cluster, previous) && !add_entry(map, index, cluster)) {
            return SCAN_FULL;
        }
        previous = cluster;
        if (index + 1 < map->clusters && !fat_chain_next(volume, cluster, &cluster)) {
            return SCAN_BROKEN;
        }
    }
    return SCAN_DONE;
}

static void build_map(FatVolume *volume, const FatFile *file, FatExtents *map)
{
    *map = (FatExtents){.clusters = file_clusters(volume, file)};
    ScanResult result = scan_chain(volume, file, map);
    if (result == SCAN_FULL) {
        map->count = 0;
        map->stride = (map->clusters + FAT_EXTENT_CAPACITY - 1) / FAT_EXTENT_CAPACITY;
        result = scan_chain(volume, file, map);
    }
    map->state = result == SCAN_DONE ? FAT_EXTENTS_READY : FAT_EXTENTS_FAILED;
}

static bool needs_map(const FatFile *file, uint32_t index)
{
    if (file->cursor_cluster == 0) {
        return index > WALK_AHEAD_CLUSTERS;
    }
    return index < file->cursor_index || index - file->cursor_index > WALK_AHEAD_CLUSTERS;
}

static const FatExtent *base_entry(const FatExtents *map, uint32_t index)
{
    uint32_t position = map->count;
    while (position > 1 && map->entry[position - 1].index > index) {
        position--;
    }
    return &map->entry[position - 1];
}

static FatLookup checkpoint_lookup(FatVolume *volume, FatFile *file, uint32_t index,
                                   uint32_t *cluster)
{
    const FatExtent *base = base_entry(file->extents, index);
    uint32_t at = base->index;
    uint32_t current = base->cluster;
    if (file->cursor_cluster != 0 && file->cursor_index >= at && file->cursor_index <= index) {
        at = file->cursor_index;
        current = file->cursor_cluster;
    }
    for (; at < index; at++) {
        if (!fat_next_cluster(volume, current, &current)) {
            file->cursor_cluster = 0;
            return FAT_LOOKUP_FAILED;
        }
    }
    file->cursor_index = at;
    file->cursor_cluster = current;
    *cluster = current;
    return FAT_LOOKUP_FOUND;
}

static FatLookup mapped_lookup(FatVolume *volume, FatFile *file, uint32_t index, uint32_t *cluster)
{
    const FatExtents *map = file->extents;
    if (index >= map->clusters || map->count == 0) {
        return FAT_LOOKUP_FAILED;
    }
    if (map->stride != 0) {
        return checkpoint_lookup(volume, file, index, cluster);
    }
    const FatExtent *base = base_entry(map, index);
    *cluster = base->cluster + (index - base->index);
    return FAT_LOOKUP_FOUND;
}

FatLookup fat_extents_lookup(FatVolume *volume, FatFile *file, uint32_t index, uint32_t *cluster)
{
    FatExtents *map = file->extents;
    if (map == NULL || map->state == FAT_EXTENTS_FAILED) {
        return FAT_LOOKUP_UNMAPPED;
    }
    if (map->state == FAT_EXTENTS_IDLE) {
        if (!needs_map(file, index)) {
            return FAT_LOOKUP_UNMAPPED;
        }
        build_map(volume, file, map);
    }
    if (map->state == FAT_EXTENTS_FAILED) {
        return FAT_LOOKUP_UNMAPPED;
    }
    return mapped_lookup(volume, file, index, cluster);
}
