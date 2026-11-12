#pragma once

#include "hal/power.h"

enum {
    CHARGE_RATE_WINDOW = 11,
    CHARGE_RATE_MINUTE_MS = 60000
};

typedef struct ChargeRate {
    uint32_t sampled_ms;
    uint32_t times[CHARGE_RATE_WINDOW];
    uint8_t levels[CHARGE_RATE_WINDOW];
    uint8_t count;
    uint8_t next;
    uint8_t rate;
    uint8_t candidate;
    uint8_t agreement;
    uint8_t stable_windows;
    uint16_t minutes;
    uint16_t filtered;
    bool active;
    bool computer;
} ChargeRate;

void charge_rate_update(ChargeRate *rate, HalBattery battery, uint32_t now_ms, bool computer);
