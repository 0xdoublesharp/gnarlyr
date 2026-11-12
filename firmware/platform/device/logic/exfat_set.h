#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/fat.h"

enum {
    ENTRY_END = 0x00,
    ENTRY_FILE = 0x85,
    ENTRY_STREAM = 0xC0,
    MODIFIED_TIMESTAMP = 12,
    MODIFIED_UTC_OFFSET = 23,
    UTC_OFFSET_VALID = 0x80,
    UTC_OFFSET_NEGATIVE = 0x40,
    UTC_OFFSET_QUARTERS = 0x7F,
    UTC_OFFSET_RANGE = 0x80,
    SECONDS_PER_QUARTER_HOUR = 900,
    SECONDS_PER_DAY = 86400,
    DAYS_PER_ERA = 146097,
    YEARS_PER_ERA = 400,
    DAYS_BEFORE_UNIX_EPOCH = 719468,
    FIRST_FAT_YEAR = 1980,
    ENTRY_NAME = 0xC1,
    ATTRIBUTE_HIDDEN = 0x02,
    ATTRIBUTE_DIRECTORY = 0x10,
    FLAG_NO_FAT_CHAIN = 0x02,
    NAME_UNITS_PER_ENTRY = 15,
    MINIMUM_SECONDARIES = 2,
    MAXIMUM_SECONDARIES = 18,
    MAXIMUM_DIRECTORY_ENTRIES = 0x100000
};

typedef struct EntrySet {
    uint32_t first_cluster;
    uint32_t valid_low;
    uint32_t valid_high;
    uint32_t data_low;
    uint32_t data_high;
    uint8_t secondaries;
    uint8_t name_length;
    uint32_t modified;
    uint8_t utc_offset;
    bool directory;
    bool hidden;
    bool contiguous;
} EntrySet;

bool exfat_read_set(FatVolume *volume, FatFile *directory, uint32_t index, EntrySet *set);
void exfat_read_names(FatVolume *volume, FatFile *directory, uint32_t first, const EntrySet *set);
bool exfat_file_from_set(const EntrySet *set, FatFile *found);
