#include <stdbool.h>
#include <stddef.h>

#include "device/fat.h"
#include "hal/storage.h"
#include "storage_reset.h"

bool hal_file_remove(const char *path)
{
    FatVolume *volume = board_music_volume();
    return volume != NULL && fat_delete(volume, path);
}
