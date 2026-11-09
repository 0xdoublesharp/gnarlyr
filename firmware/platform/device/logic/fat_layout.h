#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device/fat.h"

typedef struct FatEntryLocation {
    uint32_t index;
    uint32_t count;
    uint32_t first_cluster;
    uint32_t length;
    bool directory;
    bool contiguous;
} FatEntryLocation;

uint16_t fat_le16(const uint8_t *bytes);
uint32_t fat_le32(const uint8_t *bytes);
const uint8_t *fat_cached_sector(const FatVolume *volume);
bool fat_read_sector(FatVolume *volume, uint32_t lba);
bool fat_sector_cached(FatVolume *volume, uint32_t lba);
bool fat_read_run(FatVolume *volume, uint32_t lba, uint32_t count);
uint32_t fat_run_length(FatVolume *volume, FatFile *file, uint32_t sector_index, uint32_t limit);
uint8_t *fat_sector_for_update(FatVolume *volume, uint32_t lba);
bool fat_commit_sector(FatVolume *volume, uint32_t lba);
bool fat_write_run(FatVolume *volume, uint32_t lba, const uint8_t *bytes, uint32_t count);
bool fat_clear_sectors(FatVolume *volume, uint32_t lba, uint32_t count);
bool fat_update_bytes(FatVolume *volume, uint32_t lba, uint32_t within, const uint8_t *bytes,
                      uint32_t length);
uint32_t fat_cluster_lba(const FatVolume *volume, uint32_t cluster);
bool fat_find_entry(FatVolume *volume, FatFile *directory, const char *name, size_t length,
                    FatFile *found);
FatFile fat_root_directory(const FatVolume *volume);
const uint8_t *fat_read_entry(FatVolume *volume, FatFile *directory, uint32_t index);
bool fat_locate_entry(FatVolume *volume, FatFile *directory, const char *name, size_t length,
                      FatEntryLocation *where);
bool fat_next_cluster(FatVolume *volume, uint32_t cluster, uint32_t *next);
bool fat_chain_next(FatVolume *volume, uint32_t cluster, uint32_t *next);
uint32_t fat_info_next_free(FatVolume *volume);
bool fat_info_record(FatVolume *volume, uint32_t next_free, uint32_t count, bool released);
