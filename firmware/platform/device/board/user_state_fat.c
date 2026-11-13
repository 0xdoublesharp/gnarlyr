#include "user_state_fat.h"

#include <stddef.h>

static FatVolume *volume_of(void *context, const UserStatePlace *place)
{
    const UserStateDisk *disk = context;
    return place->volume == USER_STATE_MUSIC ? disk->music : disk->internal;
}

static bool open_file(void *context, const UserStatePlace *place, FatFile *file)
{
    FatVolume *volume = volume_of(context, place);
    return volume != NULL && fat_open(volume, place->path, file) && !file->directory;
}

static uint32_t file_size(void *context, const UserStatePlace *place)
{
    FatFile file;
    return open_file(context, place, &file) ? file.size : USER_STATE_MISSING;
}

static bool file_read(void *context, const UserStatePlace *place, UserStateChunk chunk)
{
    FatFile file;
    return open_file(context, place, &file) &&
           fat_read(volume_of(context, place), &file, chunk.offset, chunk.bytes, chunk.length);
}

static bool file_write(void *context, const UserStatePlace *place, UserStateChunk chunk)
{
    FatFile file;
    return open_file(context, place, &file) &&
           fat_write(volume_of(context, place), &file, chunk.offset, chunk.bytes, chunk.length);
}

static bool file_remove(void *context, const UserStatePlace *place)
{
    FatVolume *volume = volume_of(context, place);
    return volume != NULL && fat_delete(volume, place->path);
}

static bool file_create(void *context, const UserStatePlace *place, uint32_t size)
{
    uint32_t found = file_size(context, place);
    if (found == size) {
        return true;
    }
    if (found != USER_STATE_MISSING && !file_remove(context, place)) {
        return false;
    }
    FatVolume *volume = volume_of(context, place);
    return volume != NULL && fat_reserve(volume, place->path, size) == FAT_RESERVE_CREATED;
}

UserStateIo user_state_fat_io(UserStateDisk *disk, uint8_t *buffer, uint32_t buffer_bytes)
{
    return (UserStateIo){file_size,   file_read, file_create, file_write,
                         file_remove, disk,      buffer,      buffer_bytes};
}
