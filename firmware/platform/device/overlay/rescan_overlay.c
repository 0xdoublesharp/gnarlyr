#include <stddef.h>

#include "app/rescan_module.h"
#include "storage_listing.h"

const RescanModuleEntry *rescan_overlay_entry(void);
bool rescan_overlay_next_entry(HalDir *dir, ScanEntry *entry);

const RescanModuleEntry *rescan_overlay_entry(void)
{
    return &RESCAN_MODULE_ENTRY;
}

bool rescan_overlay_next_entry(HalDir *dir, ScanEntry *entry)
{
    FatVolume *volume;
    FatDirCursor *cursor;
    FatDirEntry *listed = storage_listing_begin(dir, &volume, &cursor);
    if (listed == NULL || !fat_next_entry(volume, cursor, listed)) {
        return false;
    }
    entry->directory = listed->file.directory;
    entry->size = listed->listed_size;
    entry->modified = listed->modified;
    entry->cluster = listed->file.first_cluster;
    if (entry->directory) {
        HalDirPlace at = hal_dir_place(dir);
        entry->place = (ScanPlace){at.start, at.size, at.flags};
    }
    uint32_t index = 0;
    while (index + 1 < sizeof entry->name && listed->name[index] != '\0') {
        entry->name[index] = listed->name[index];
        index++;
    }
    entry->name[index] = '\0';
    return true;
}
