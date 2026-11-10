#pragma once

#include <stdint.h>

#include "settings/settings.h"

uint32_t equalizer_peak_grid_tangent(int point);
uint64_t equalizer_peak_cascade(const int8_t *gains, const uint32_t *tangents, int count,
                                uint32_t grid);
uint16_t equalizer_peak_pre_gain(const int8_t gains[SETTINGS_EQUALIZER_BANDS]);
