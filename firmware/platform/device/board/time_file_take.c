#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "core/seam.h"
#include "device/time_file.h"
#include "emmc.h"
#include "internal_volume.h"
#include "usb_session.h"

CORE_SEAM void time_file_take(bool apply)
{
    EmmcInfo info;
    FatVolume *volume = emmc_start(&info) ? internal_volume_remount() : NULL;
    FatFile file;
    if (volume == NULL || !fat_open(volume, TIME_FILE_PATH, &file)) {
        return;
    }
    uint8_t bytes[TIME_FILE_BYTES];
    HalWallTime time;
    bool read =
        apply && file.size == TIME_FILE_BYTES && fat_read(volume, &file, 0, bytes, sizeof bytes);
    fat_delete(volume, TIME_FILE_PATH);
    if (read && time_file_parse(bytes, &time)) {
        board_wall_time_set(time);
    }
}
