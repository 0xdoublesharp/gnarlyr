#include "device/clock_governor.h"

enum {
    PERCENT = 100,
    STEP = 2
};

static void restart(ClockGovernor *governor, ClockGovernorReading reading)
{
    governor->started_us = reading.now_us;
    governor->started_idle = reading.idle;
}

void clock_governor_start(ClockGovernor *governor, const ClockGovernorLimits *limits,
                          ClockGovernorReading reading)
{
    *governor = (ClockGovernor){.limits = limits, .divider = limits->lowest_divider};
    restart(governor, reading);
}

void clock_governor_hurry(ClockGovernor *governor, ClockGovernorReading reading)
{
    governor->divider = governor->limits->lowest_divider;
    restart(governor, reading);
}

static uint32_t idle_per_us(const ClockGovernor *governor)
{
    uint32_t pll_mhz = governor->limits->pll_mhz;
    return pll_mhz == 0 ? 1 : pll_mhz / governor->divider;
}

static uint32_t busy_percent(const ClockGovernor *governor, uint32_t elapsed_us, uint32_t idle)
{
    uint64_t capacity = (uint64_t)elapsed_us * idle_per_us(governor);
    if (capacity == 0 || idle >= capacity) {
        return 0;
    }
    return PERCENT - (uint32_t)((uint64_t)idle * PERCENT / capacity);
}

static uint32_t next_divider(const ClockGovernor *governor, uint32_t busy)
{
    const ClockGovernorLimits *limits = governor->limits;
    uint32_t divider = governor->divider;
    if (busy > limits->raise_percent && divider / STEP >= limits->lowest_divider) {
        return divider / STEP;
    }
    if (divider * STEP <= limits->highest_divider && busy * STEP <= limits->deepen_percent) {
        return divider * STEP;
    }
    return divider;
}

bool clock_governor_update(ClockGovernor *governor, ClockGovernorReading reading)
{
    uint32_t elapsed_us = reading.now_us - governor->started_us;
    if (elapsed_us < governor->limits->window_us) {
        return false;
    }
    governor->busy_percent =
        busy_percent(governor, elapsed_us, reading.idle - governor->started_idle);
    uint32_t divider = next_divider(governor, governor->busy_percent);
    restart(governor, reading);
    if (divider == governor->divider) {
        return false;
    }
    governor->divider = divider;
    return true;
}
