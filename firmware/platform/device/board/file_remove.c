#include <stdbool.h>
#include <stddef.h>

#include "board.h"
#include "device/fat.h"
#include "device/time_file.h"
#include "hal/storage.h"
#include "internal_volume.h"
#include "storage_reset.h"

bool hal_file_remove(const char *path)
{
    FatVolume *volume = board_music_volume();
    return volume != NULL && fat_delete(volume, path);
}

void board_wall_time_delete_file(void)
{
    FatVolume *volume = internal_volume_mount();
    if (volume != NULL) {
        (void)fat_delete(volume, TIME_FILE_PATH);
    }
}
