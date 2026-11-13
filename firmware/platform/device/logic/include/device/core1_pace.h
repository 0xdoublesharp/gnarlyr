#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/clock_governor.h"

enum {
    CORE1_PACE_UNIT_MHZ = 100,
    CORE1_PACE_PLL_MHZ = 200,
    CORE1_PACE_IDLE_MHZ = 100,
    CORE1_PACE_REST_US = 5000,
    CORE1_PACE_ROOM_REST_US = 20000
};

uint32_t core1_pace_units(uint32_t cycles, uint32_t mhz);
uint32_t core1_pace_rest_us(bool waiting_for_room);
uint32_t core1_pace_rest_mhz(uint32_t mhz);
uint32_t core1_pace_divider_field(uint32_t mhz);
const ClockGovernorLimits *core1_pace_limits(uint32_t ceiling_mhz);
uint32_t core1_pace_mhz(uint32_t divider);
