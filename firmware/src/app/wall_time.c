#include "wall_time.h"

#include "core/clock_face.h"
#include "hal/clock.h"

WallNow wall_time_now(void)
{
    HalWallTime time = hal_clock_wall();
    if (time.source == HAL_WALL_UNKNOWN) {
        return (WallNow){.known = false};
    }
    return (WallNow){.known = true,
                     .local_s = clock_local_seconds(time.utc_s, time.offset_minutes)};
}

bool wall_time_24h(const Settings *settings)
{
    (void)settings;
    return true;
}
