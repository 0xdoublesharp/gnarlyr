#include "exfat.h"

#include "exfat_set.h"
#include "fat_layout.h"
#include "fat_names.h"

static bool read_stream(FatVolume *volume, FatFile *directory, uint32_t index, EntrySet *set)
{
    const uint8_t *stream = fat_read_entry(volume, directory, index);
    if (stream == NULL || stream[0] != ENTRY_STREAM) {
        return false;
    }
    set->contiguous = (stream[1] & FLAG_NO_FAT_CHAIN) != 0;
    set->name_length = stream[3];
    set->valid_low = fat_le32(stream + 8);
    set->valid_high = fat_le32(stream + 12);
    set->first_cluster = fat_le32(stream + 20);
    set->data_low = fat_le32(stream + 24);
    set->data_high = fat_le32(stream + 28);
    return true;
}

void exfat_read_names(FatVolume *volume, FatFile *directory, uint32_t first, const EntrySet *set)
{
    uint32_t stored = 0;
    for (uint32_t secondary = 1; secondary < set->secondaries; secondary++) {
        const uint8_t *entry = fat_read_entry(volume, directory, first + secondary);
        if (entry == NULL || entry[0] != ENTRY_NAME) {
            continue;
        }
        for (uint32_t unit = 0; unit < NAME_UNITS_PER_ENTRY; unit++) {
            if (stored < FAT_LONG_NAME_CAPACITY) {
                volume->long_name[stored++] = fat_le16(entry + 2 + 2 * unit);
            }
        }
    }
}

bool exfat_file_from_set(const EntrySet *set, FatFile *found)
{
    uint32_t high = set->directory ? set->data_high : set->valid_high | set->data_high;
    uint32_t size = set->directory ? set->data_low : set->valid_low;
    if (high != 0 || (set->directory && size == 0)) {
        return false;
    }
    *found = (FatFile){.first_cluster = set->first_cluster,
                       .size = size,
                       .directory = set->directory,
                       .contiguous = set->contiguous};
    return true;
}

bool exfat_read_set(FatVolume *volume, FatFile *directory, uint32_t index, EntrySet *set)
{
    const uint8_t *entry = fat_read_entry(volume, directory, index);
    set->secondaries = entry[1];
    set->directory = (fat_le16(entry + 4) & ATTRIBUTE_DIRECTORY) != 0;
    set->hidden = (fat_le16(entry + 4) & ATTRIBUTE_HIDDEN) != 0;
    set->modified = fat_le32(entry + MODIFIED_TIMESTAMP);
    set->utc_offset = entry[MODIFIED_UTC_OFFSET];
    return set->secondaries >= MINIMUM_SECONDARIES && set->secondaries <= MAXIMUM_SECONDARIES &&
           read_stream(volume, directory, index + 1, set);
}

static void note_location(FatEntryLocation *where, uint32_t index, const EntrySet *set)
{
    if (where != NULL) {
        *where = (FatEntryLocation){.index = index,
                                    .count = 1u + set->secondaries,
                                    .first_cluster = set->first_cluster,
                                    .length = set->data_low,
                                    .directory = set->directory,
                                    .contiguous = set->contiguous};
    }
}

bool exfat_find_entry(FatVolume *volume, FatFile *directory, const char *name, size_t length,
                      FatFile *found, FatEntryLocation *where)
{
    uint32_t index = 0;
    while (index < MAXIMUM_DIRECTORY_ENTRIES) {
        const uint8_t *entry = fat_read_entry(volume, directory, index);
        if (entry == NULL || entry[0] == ENTRY_END) {
            return false;
        }
        EntrySet set = {.secondaries = 0};
        if (entry[0] != ENTRY_FILE || !exfat_read_set(volume, directory, index, &set)) {
            index++;
            continue;
        }
        exfat_read_names(volume, directory, index + 1, &set);
        if (fat_name_matches(volume->long_name, set.name_length, name, length)) {
            note_location(where, index, &set);
            return exfat_file_from_set(&set, found);
        }
        index += 1u + set.secondaries;
    }
    return false;
}
