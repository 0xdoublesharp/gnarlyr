#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device/fat.h"

enum {
    ENTRY_SIZE = 32,
    ENTRY_END = 0x00,
    ENTRY_DELETED = 0xE5,
    ATTRIBUTES = 11,
    WRITE_TIME = 22,
    WRITE_DATE = 24,
    ATTRIBUTE_HIDDEN = 0x02,
    ATTRIBUTE_VOLUME_ID = 0x08,
    ATTRIBUTE_DIRECTORY = 0x10,
    ATTRIBUTE_LONG_NAME = 0x0F,
    LONG_NAME_LAST = 0x40,
    LONG_NAME_ORDINAL_MASK = 0x1F,
    LONG_NAME_MAX_ENTRIES = 20,
    LONG_NAME_CHECKSUM = 13,
    CHARACTERS_PER_ENTRY = 13,
    SHORT_NAME_LENGTH = 11,
    SHORT_BASE_LENGTH = 8,
    KANJI_E5_MARKER = 0x05,
    CASE_FLAGS = 12,
    LOWER_BASE = 0x08,
    LOWER_EXTENSION = 0x10,
    MAXIMUM_DIRECTORY_ENTRIES = 65536
};

typedef struct LongName {
    bool valid;
    uint8_t checksum;
    uint8_t expected;
    uint8_t entries;
    uint32_t start;
} LongName;

void fat_collect_long_name(FatVolume *volume, LongName *name, const uint8_t *entry,
                           uint32_t position);
bool fat_long_name_applies(const LongName *name, const uint8_t *entry);
size_t fat_trimmed_length(const uint8_t *text, size_t length);
size_t fat_short_name_text(const uint8_t *entry, char *text);
FatFile fat_entry_file(const FatVolume *volume, const uint8_t *entry);
