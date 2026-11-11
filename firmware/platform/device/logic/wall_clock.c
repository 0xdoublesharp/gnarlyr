#include "device/wall_clock.h"

void wall_clock_set(WallClock *clock, HalWallTime time, uint32_t now_ms)
{
    *clock = (WallClock){.base_s = time.utc_s,
                         .base_ms = now_ms,
                         .offset_minutes = time.offset_minutes,
                         .source = time.source};
}

HalWallTime wall_clock_now(WallClock *clock, uint32_t now_ms)
{
    if (clock->source == HAL_WALL_UNKNOWN) {
        return (HalWallTime){.source = HAL_WALL_UNKNOWN};
    }
    uint32_t seconds = (now_ms - clock->base_ms) / WALL_CLOCK_SECOND_MS;
    clock->base_s += seconds;
    clock->base_ms += seconds * WALL_CLOCK_SECOND_MS;
    return (HalWallTime){
        .utc_s = clock->base_s, .offset_minutes = clock->offset_minutes, .source = clock->source};
}
