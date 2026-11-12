#include "device/fat.h"

#include "exfat.h"
#include "fat_entry.h"
#include "fat_layout.h"
#include "fat_names.h"

static void lower_ascii(char *text, size_t length)
{
    for (size_t index = 0; index < length; index++) {
        if (text[index] >= 'A' && text[index] <= 'Z') {
            text[index] = (char)(text[index] - 'A' + 'a');
        }
    }
}

static void short_name_utf8(const uint8_t *entry, char *out)
{
    size_t base = fat_trimmed_length(entry, SHORT_BASE_LENGTH);
    size_t length = fat_short_name_text(entry, out);
    out[length] = '\0';
    if ((entry[CASE_FLAGS] & LOWER_BASE) != 0) {
        lower_ascii(out, base);
    }
    if ((entry[CASE_FLAGS] & LOWER_EXTENSION) != 0 && length > base) {
        lower_ascii(out + base, length - base);
    }
}

static bool is_dot_entry(const uint8_t *entry)
{
    return entry[0] == '.' && (entry[1] == ' ' || (entry[1] == '.' && entry[2] == ' '));
}

static void fill_entry(FatVolume *volume, const LongName *long_name, const uint8_t *entry,
                       FatDirEntry *found)
{
    found->file = fat_entry_file(volume, entry);
    found->hidden = (entry[ATTRIBUTES] & ATTRIBUTE_HIDDEN) != 0;
    found->modified = (uint32_t)fat_le16(entry + WRITE_DATE) << 16 | fat_le16(entry + WRITE_TIME);
    found->listed_size = found->file.size;
    if (fat_long_name_applies(long_name, entry)) {
        size_t limit = (size_t)long_name->entries * CHARACTERS_PER_ENTRY;
        fat_utf8_from_units(volume->long_name, limit, found->name, sizeof found->name);
    } else {
        short_name_utf8(entry, found->name);
    }
}

static bool next_fat_entry(FatVolume *volume, FatDirCursor *cursor, FatDirEntry *found)
{
    LongName long_name = {.valid = false};
    while (cursor->index < MAXIMUM_DIRECTORY_ENTRIES) {
        const uint8_t *entry = fat_read_entry(volume, &cursor->directory, cursor->index);
        if (entry == NULL || entry[0] == ENTRY_END) {
            return false;
        }
        cursor->index++;
        if (entry[0] == ENTRY_DELETED) {
            long_name.valid = false;
        } else if (entry[ATTRIBUTES] == ATTRIBUTE_LONG_NAME) {
            fat_collect_long_name(volume, &long_name, entry, cursor->index - 1);
        } else if ((entry[ATTRIBUTES] & ATTRIBUTE_VOLUME_ID) != 0 || is_dot_entry(entry)) {
            long_name.valid = false;
        } else {
            fill_entry(volume, &long_name, entry, found);
            return true;
        }
    }
    return false;
}

bool fat_next_entry(FatVolume *volume, FatDirCursor *cursor, FatDirEntry *entry)
{
    if (volume->type == FAT_TYPE_EXFAT) {
        return exfat_next_entry(volume, cursor, entry);
    }
    return next_fat_entry(volume, cursor, entry);
}
