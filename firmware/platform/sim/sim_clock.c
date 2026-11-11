#include "sim_clock.h"

#include <SDL.h>
#include <stdbool.h>
#include <time.h>

#include "hal/clock.h"

enum {
    MICROSECONDS_PER_SECOND = 1000000
};

enum {
    MILLISECONDS_PER_SECOND = 1000,
    SECONDS_PER_MINUTE = 60
};

typedef struct SimClock {
    bool virtual_time;
    uint32_t virtual_ms;
} SimClock;

typedef struct SimWall {
    SimClockMode mode;
    uint32_t utc_s;
    uint32_t set_ms;
    int16_t offset_minutes;
} SimWall;

static SimClock clock_state;
static SimWall wall_state;

void sim_clock_use_virtual_time(uint32_t start_ms)
{
    clock_state = (SimClock){.virtual_time = true, .virtual_ms = start_ms};
}

void sim_clock_advance(uint32_t milliseconds)
{
    clock_state.virtual_ms += milliseconds;
}

uint32_t hal_clock_milliseconds(void)
{
    return clock_state.virtual_time ? clock_state.virtual_ms : (uint32_t)SDL_GetTicks64();
}

void sim_clock_set_wall(SimClockMode mode, uint32_t utc_s, int16_t offset_minutes)
{
    wall_state = (SimWall){.mode = mode,
                           .utc_s = utc_s,
                           .set_ms = hal_clock_milliseconds(),
                           .offset_minutes = offset_minutes};
}

static HalWallTime host_time(void)
{
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    return (HalWallTime){.utc_s = (uint32_t)now,
                         .offset_minutes = (int16_t)(local.tm_gmtoff / SECONDS_PER_MINUTE),
                         .source = HAL_WALL_COMPUTER};
}

HalWallTime hal_clock_wall(void)
{
    if (wall_state.mode == SIM_CLOCK_NOW) {
        return host_time();
    }
    if (wall_state.mode == SIM_CLOCK_UNSET) {
        return (HalWallTime){.source = HAL_WALL_UNKNOWN};
    }
    uint32_t elapsed_s = (hal_clock_milliseconds() - wall_state.set_ms) / MILLISECONDS_PER_SECOND;
    return (HalWallTime){.utc_s = wall_state.utc_s + elapsed_s,
                         .offset_minutes = wall_state.offset_minutes,
                         .source = HAL_WALL_COMPUTER};
}

uint32_t hal_clock_microseconds(void)
{
    uint64_t counter = SDL_GetPerformanceCounter();
    uint64_t frequency = SDL_GetPerformanceFrequency();
    uint64_t whole_seconds = counter / frequency;
    uint64_t remainder = counter % frequency;
    return (uint32_t)(whole_seconds * MICROSECONDS_PER_SECOND +
                      remainder * MICROSECONDS_PER_SECOND / frequency);
}
