#include "exfat.h"

#include "exfat_set.h"
#include "fat_layout.h"
#include "fat_names.h"

static uint32_t days_from_civil(uint32_t year, uint32_t month, uint32_t day)
{
    uint32_t shifted = month <= 2 ? year - 1u : year;
    uint32_t era = shifted / YEARS_PER_ERA;
    uint32_t year_of_era = shifted - era * YEARS_PER_ERA;
    uint32_t day_of_year = (153u * ((month + 9u) % 12u) + 2u) / 5u + day - 1u;
    uint32_t day_of_era = year_of_era * 365u + year_of_era / 4u - year_of_era / 100u + day_of_year;
    return era * DAYS_PER_ERA + day_of_era - DAYS_BEFORE_UNIX_EPOCH;
}

static int32_t utc_offset_half_seconds(uint8_t utc_offset)
{
    if ((utc_offset & UTC_OFFSET_VALID) == 0) {
        return 0;
    }
    int32_t quarters = utc_offset & UTC_OFFSET_QUARTERS;
    if ((utc_offset & UTC_OFFSET_NEGATIVE) != 0) {
        quarters -= UTC_OFFSET_RANGE;
    }
    return quarters * (SECONDS_PER_QUARTER_HOUR / 2);
}

uint32_t fat_exfat_utc_stamp(uint32_t timestamp, uint8_t utc_offset)
{
    uint32_t date = timestamp >> 16;
    uint32_t days = days_from_civil(FIRST_FAT_YEAR + (date >> 9), date >> 5 & 15u, date & 31u);
    uint32_t clock =
        (timestamp >> 11 & 31u) * 1800u + (timestamp >> 5 & 63u) * 30u + (timestamp & 31u);
    uint32_t local = days * (SECONDS_PER_DAY / 2) + clock;
    return local - (uint32_t)utc_offset_half_seconds(utc_offset);
}

bool exfat_next_entry(FatVolume *volume, FatDirCursor *cursor, FatDirEntry *found)
{
    while (cursor->index < MAXIMUM_DIRECTORY_ENTRIES) {
        uint32_t index = cursor->index;
        const uint8_t *entry = fat_read_entry(volume, &cursor->directory, index);
        if (entry == NULL || entry[0] == ENTRY_END) {
            return false;
        }
        EntrySet set = {.secondaries = 0};
        if (entry[0] != ENTRY_FILE || !exfat_read_set(volume, &cursor->directory, index, &set)) {
            cursor->index++;
            continue;
        }
        cursor->index += 1u + set.secondaries;
        exfat_read_names(volume, &cursor->directory, index + 1, &set);
        if (exfat_file_from_set(&set, &found->file)) {
            found->hidden = set.hidden;
            found->modified = fat_exfat_utc_stamp(set.modified, set.utc_offset);
            found->listed_size = set.data_low;
            fat_utf8_from_units(volume->long_name, set.name_length, found->name,
                                sizeof found->name);
            return true;
        }
    }
    return false;
}
