#include "board.h"
#include "core/seam.h"
#include "device/time_file.h"
#include "device/wall_clock.h"
#include "extension_entry.h"
#include "hal/clock.h"
#include "hal/system.h"

static WallClock wall;

CORE_SEAM void board_wall_time_set(HalWallTime time)
{
    wall_clock_set(&wall, time, hal_clock_milliseconds());
}

HalWallTime hal_clock_wall(void)
{
    return wall_clock_now(&wall, hal_clock_milliseconds());
}

void board_wall_time_keep(void)
{
    (void)hal_clock_wall();
}

CORE_SEAM void board_wall_time_forget_file(void)
{
    const ExtensionModuleEntry *code = hal_system_extension_code();
    if (code != NULL) {
        code->forget_time_file();
    }
}
