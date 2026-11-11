#include "app/app.h"
#include "board.h"
#include "core/deadline.h"
#include "core/seam.h"
#include "device/time_file.h"
#include "device/wall_clock.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "internal_volume.h"

static WallClock wall;
static bool restore_pending;
static uint32_t restore_due_ms;

void board_wall_time_start(void)
{
    restore_due_ms = hal_clock_milliseconds() + 2000;
    restore_pending = true;
}

CORE_SEAM void board_wall_time_set(HalWallTime time)
{
    wall_clock_set(&wall, time, hal_clock_milliseconds());
}

HalWallTime hal_clock_wall(void)
{
    return wall_clock_now(&wall, hal_clock_milliseconds());
}

CORE_SEAM void board_wall_time_restore(void)
{
    if (!restore_pending) {
        return;
    }
    if (wall.source != HAL_WALL_UNKNOWN) {
        restore_pending = false;
        return;
    }
    if (!deadline_reached(hal_clock_milliseconds(), restore_due_ms) || app_preparing() ||
        app_pass_interval_ms() <= 1) {
        return;
    }
    HalAudioStatus audio = hal_audio_status();
    if (audio.state != HAL_AUDIO_STOPPED || !audio.idle) {
        return;
    }
    restore_pending = false;
    (void)hal_clock_restore_wall(app_clock_offset());
}

void board_wall_time_keep(void)
{
    (void)hal_clock_wall();
    board_wall_time_restore();
}

CORE_SEAM void board_wall_time_forget_file(void)
{
    FatVolume *volume = internal_volume_mount();
    if (volume != NULL) {
        (void)fat_delete(volume, TIME_FILE_PATH);
    }
}
