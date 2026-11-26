#pragma once

#include "core/battery_estimate.h"

enum {
    BATTERY_MODEL_BYTES = 32
};

extern const char BATTERY_MODEL_PATH[];

bool battery_model_decode(BatteryEstimate *estimate, const uint8_t bytes[BATTERY_MODEL_BYTES]);
bool battery_model_save(BatteryEstimate *estimate);
