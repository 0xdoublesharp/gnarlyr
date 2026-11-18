#include <stddef.h>

#include "core/drive_path.h"
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

enum {
    PLACE_KNOWN = 1,
    PLACE_CONTIGUOUS = 2
};

HalDirPlace hal_dir_place(const HalDir *dir)
{
    FatVolume *volume = NULL;
    FatDirCursor *cursor = NULL;
    const FatDirEntry *listed = storage_listing_begin(dir, &volume, &cursor);
    if (listed == NULL || !listed->file.directory) {
        return (HalDirPlace){.flags = 0};
    }
    return (HalDirPlace){.start = listed->file.first_cluster,
                         .size = listed->file.size,
                         .flags = PLACE_KNOWN | (listed->file.contiguous ? PLACE_CONTIGUOUS : 0u)};
}

static void remember_folder(FatVolume *volume, const char *path, const FatFile *folder)
{
    uint32_t length = 0;
    for (; path[length] != '\0' && length < FAT_PARENT_CAPACITY; length++) {
        volume->parent_path[length] = path[length];
    }
    volume->parent_valid = path[length] == '\0';
    volume->parent_length = length - (length > 0 && path[length - 1] == '/' ? 1u : 0u);
    volume->parent = *folder;
    volume->parent_named = true;
}

bool hal_dir_open_place(HalDir *dir, const char *path, HalDirPlace place)
{
    if ((place.flags & PLACE_KNOWN) == 0) {
        return hal_dir_open(dir, path);
    }
    FatVolume *volume = NULL;
    FatDirCursor *cursor = NULL;
    const char *internal = drive_path_internal(path);
    if (!hal_dir_open(dir, internal != NULL ? DRIVE_PATH_INTERNAL_ROOT : "/") ||
        storage_listing_begin(dir, &volume, &cursor) == NULL) {
        return false;
    }
    FatFile folder = {.first_cluster = place.start,
                      .size = place.size,
                      .directory = true,
                      .contiguous = (place.flags & PLACE_CONTIGUOUS) != 0};
    *cursor = (FatDirCursor){.directory = folder};
    remember_folder(volume, internal != NULL ? internal : path, &folder);
    return true;
}
