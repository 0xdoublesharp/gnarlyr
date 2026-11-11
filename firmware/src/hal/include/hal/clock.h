#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum HalWallSource {
    HAL_WALL_UNKNOWN,
    HAL_WALL_COMPUTER,
    HAL_WALL_PLAYER
} HalWallSource;

typedef struct HalWallTime {
    uint32_t utc_s;
    int16_t offset_minutes;
    uint8_t source;
} HalWallTime;

uint32_t hal_clock_milliseconds(void);
uint32_t hal_clock_microseconds(void);
HalWallTime hal_clock_wall(void);
bool hal_clock_set_wall(HalWallTime time);
bool hal_clock_restore_wall(int16_t offset_minutes);
