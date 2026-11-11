#pragma once

#include <stdint.h>

#include "hal/clock.h"

enum {
    WALL_CLOCK_SECOND_MS = 1000
};

typedef struct WallClock {
    uint32_t base_s;
    uint32_t base_ms;
    int16_t offset_minutes;
    uint8_t source;
} WallClock;

void wall_clock_set(WallClock *clock, HalWallTime time, uint32_t now_ms);
HalWallTime wall_clock_now(WallClock *clock, uint32_t now_ms);
