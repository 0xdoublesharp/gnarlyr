#include <stddef.h>

#include "device/fat.h"
#include "hal/storage.h"
#include "storage_listing.h"

bool hal_dir_next(HalDir *dir, HalDirEntry *entry)
{
    FatVolume *volume = NULL;
    FatDirCursor *cursor = NULL;
    FatDirEntry *listed = storage_listing_begin(dir, &volume, &cursor);
    if (listed == NULL || !fat_next_entry(volume, cursor, listed)) {
        return false;
    }
    entry->directory = listed->file.directory;
    entry->hidden = listed->hidden;
    entry->size = listed->file.directory ? 0 : listed->listed_size;
    entry->modified = listed->modified;
    for (size_t index = 0; index < sizeof entry->name; index++) {
        entry->name[index] = listed->name[index];
    }
    return true;
}
