#include <stdbool.h>
#include <stdint.h>

#include "app/app.h"
#include "app/debug_loop.h"
#include "board.h"
#include "core/seam.h"
#include "device/time_file.h"
#include "emmc.h"
#include "extension_entry.h"
#include "internal_volume.h"
#include "usb_session.h"

static bool clock_chip_set(uint32_t utc_s)
{
    const ExtensionModuleEntry *code = hal_system_extension_code();
    return code != NULL && code->rtc_set(utc_s).status == HAL_RTC_OK;
}

static bool apply_file(bool apply)
{
    EmmcInfo info;
    FatVolume *volume = emmc_start(&info) ? internal_volume_remount() : NULL;
    FatFile file;
    if (volume == NULL || !fat_open(volume, TIME_FILE_PATH, &file)) {
        return false;
    }
    uint8_t bytes[TIME_FILE_BYTES];
    HalWallTime time;
    bool read =
        apply && file.size == TIME_FILE_BYTES && fat_read(volume, &file, 0, bytes, sizeof bytes);
    fat_delete(volume, TIME_FILE_PATH);
    if (!read || !time_file_parse(bytes, &time)) {
        return false;
    }
    (void)app_store_clock_offset(time.offset_minutes, true);
    board_wall_time_set(time);
    return true;
}

CORE_SEAM void time_file_take(bool apply, bool *chip_pending)
{
    if (!apply_file(apply) && !*chip_pending) {
        return;
    }
    HalWallTime now = hal_clock_wall();
    *chip_pending = now.source != HAL_WALL_UNKNOWN && !clock_chip_set(now.utc_s);
    if (*chip_pending) {
        debug_loop_event(DEBUG_EVENT_STORAGE, DEBUG_STORAGE_CLOCK_CHIP);
    }
}
