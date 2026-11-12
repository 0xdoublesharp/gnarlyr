#pragma once

#include <stdint.h>

enum {
    POWER_HISTORY_SAMPLES = 128,
    POWER_HISTORY_INTERVAL_S = 60,
    POWER_SAMPLE_VBUS = 1,
    POWER_SAMPLE_CHARGE_PIN = 2,
    POWER_SAMPLE_PLAYING = 4,
    POWER_SAMPLE_USB_POWERED = 8,
    POWER_SAMPLE_USB_KEPT_ON = 16
};

typedef struct PowerSample {
    uint16_t minute;
    uint16_t millivolts;
    uint8_t percent;
    uint8_t flags;
} PowerSample;

typedef struct PowerHistory {
    PowerSample samples[POWER_HISTORY_SAMPLES];
    uint32_t recorded;
} PowerHistory;

void power_history_record(PowerHistory *history, PowerSample sample);
uint32_t power_history_count(const PowerHistory *history);
PowerSample power_history_at(const PowerHistory *history, uint32_t index);
