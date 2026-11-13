#include "device/core1_pace.h"

static const ClockGovernorLimits FROM_200 = {.window_us = 50000000,
                                             .pll_mhz = 0,
                                             .lowest_divider = 1,
                                             .highest_divider = 8,
                                             .deepen_percent = 30,
                                             .raise_percent = 60};
static const ClockGovernorLimits FROM_100 = {.window_us = 50000000,
                                             .pll_mhz = 0,
                                             .lowest_divider = 2,
                                             .highest_divider = 8,
                                             .deepen_percent = 30,
                                             .raise_percent = 60};

uint32_t core1_pace_units(uint32_t cycles, uint32_t mhz)
{
    if (mhz == 0 || mhz == CORE1_PACE_UNIT_MHZ) {
        return cycles;
    }
    return mhz > CORE1_PACE_UNIT_MHZ ? cycles / (mhz / CORE1_PACE_UNIT_MHZ)
                                     : cycles * (CORE1_PACE_UNIT_MHZ / mhz);
}

uint32_t core1_pace_rest_us(bool waiting_for_room)
{
    return waiting_for_room ? CORE1_PACE_ROOM_REST_US : CORE1_PACE_REST_US;
}

uint32_t core1_pace_rest_mhz(uint32_t mhz)
{
    return mhz > CORE1_PACE_IDLE_MHZ ? CORE1_PACE_IDLE_MHZ : mhz;
}

uint32_t core1_pace_divider_field(uint32_t mhz)
{
    return CORE1_PACE_PLL_MHZ / mhz - 1u;
}

const ClockGovernorLimits *core1_pace_limits(uint32_t ceiling_mhz)
{
    return ceiling_mhz == CORE1_PACE_PLL_MHZ ? &FROM_200 : &FROM_100;
}

uint32_t core1_pace_mhz(uint32_t divider)
{
    return CORE1_PACE_PLL_MHZ / divider;
}
