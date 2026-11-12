#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct ClockGovernorLimits {
    uint32_t window_us;
    uint32_t pll_mhz;
    uint8_t lowest_divider;
    uint8_t highest_divider;
    uint8_t deepen_percent;
    uint8_t raise_percent;
} ClockGovernorLimits;

typedef struct ClockGovernorReading {
    uint32_t now_us;
    uint32_t idle;
} ClockGovernorReading;

typedef struct ClockGovernor {
    const ClockGovernorLimits *limits;
    uint32_t divider;
    uint32_t started_us;
    uint32_t started_idle;
    uint32_t busy_percent;
} ClockGovernor;

void clock_governor_start(ClockGovernor *governor, const ClockGovernorLimits *limits,
                          ClockGovernorReading reading);
void clock_governor_hurry(ClockGovernor *governor, ClockGovernorReading reading);
bool clock_governor_update(ClockGovernor *governor, ClockGovernorReading reading);
