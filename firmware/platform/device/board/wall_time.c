#include "app/app.h"
#include "board.h"
#include "core/deadline.h"
#include "core/seam.h"
#include "device/wall_clock.h"
#include "extension_entry.h"
#include "hal/clock.h"
#include "hal/system.h"

enum {
    RESTORE_ATTEMPTS = 3,
    RESTORE_RETRY_MS = 2000
};

static WallClock wall;
static uint8_t restore_left;
static uint32_t restore_due_ms;

CORE_COLD static void restore_wall(void)
{
    restore_left--;
    if (hal_clock_restore_wall(app_clock_offset())) {
        restore_left = 0;
    } else {
        restore_due_ms = hal_clock_milliseconds() + RESTORE_RETRY_MS;
    }
}

void board_wall_time_start(void)
{
    if (wall.source == HAL_WALL_UNKNOWN) {
        restore_left = RESTORE_ATTEMPTS;
        restore_wall();
    }
}

CORE_SEAM void board_wall_time_set(HalWallTime time)
{
    wall_clock_set(&wall, time, hal_clock_milliseconds());
    restore_left = 0;
}

HalWallTime hal_clock_wall(void)
{
    return wall_clock_now(&wall, hal_clock_milliseconds());
}

void board_wall_time_keep(void)
{
    (void)hal_clock_wall();
    if (restore_left != 0 && deadline_reached(hal_clock_milliseconds(), restore_due_ms)) {
        restore_wall();
    }
}

CORE_SEAM void board_wall_time_forget_file(void)
{
    const ExtensionModuleEntry *code = hal_system_extension_code();
    if (code != NULL) {
        code->forget_time_file();
    }
}
