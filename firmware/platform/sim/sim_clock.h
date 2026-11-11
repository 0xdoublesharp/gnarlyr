#pragma once

#include <stdint.h>

#include "sim_options.h"

void sim_clock_use_virtual_time(uint32_t start_ms);
void sim_clock_advance(uint32_t milliseconds);
void sim_clock_set_wall(SimClockMode mode, uint32_t utc_s, int16_t offset_minutes);
