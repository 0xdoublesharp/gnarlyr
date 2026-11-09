#pragma once

#include <stdint.h>

#include "settings/settings.h"

uint16_t equalizer_peak_pre_gain(const int8_t gains[SETTINGS_EQUALIZER_BANDS]);
