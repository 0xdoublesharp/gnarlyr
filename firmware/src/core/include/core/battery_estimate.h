#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum BatteryLoadClass {
    BATTERY_LOAD_IDLE,
    BATTERY_LOAD_LIGHT,
    BATTERY_LOAD_HEAVY,
    BATTERY_LOAD_SCREEN,
    BATTERY_LOAD_COUNT
} BatteryLoadClass;

enum {
    BATTERY_ESTIMATE_SAMPLE_MS = 30000,
    BATTERY_ESTIMATE_MIN_RATE = 50,
    BATTERY_ESTIMATE_MAX_RATE = 5000,
    BATTERY_ESTIMATE_HOURS_AT = 100
};

typedef struct BatteryEstimate {
    uint32_t since_ms;
    uint32_t anchor_ms;
    uint32_t last_sample_ms;
    uint32_t usb_start_ms;
    uint32_t usb_duration_ms;
    uint16_t habit;
    uint16_t rate[BATTERY_LOAD_COUNT];
    uint16_t anchor_level;
    uint16_t settled_level;
    uint16_t last_mv;
    uint16_t shown_minutes;
    uint16_t session_rate;
    uint8_t learned[BATTERY_LOAD_COUNT];
    uint8_t session_windows;
    uint8_t active_class;
    uint8_t candidate_class;
    uint8_t effective_class;
    uint8_t shown_hours;
    bool show_time;
    bool sampled;
    bool on_usb;
    bool anchored;
    bool dirty;
    bool loaded;
    bool level_known;
} BatteryEstimate;

_Static_assert(sizeof(BatteryEstimate) <= 56, "battery estimate RAM budget");

typedef struct BatteryEstimateReading {
    uint32_t now_ms;
    uint32_t millivolts;
    BatteryLoadClass load;
    bool known;
    bool usb_power;
} BatteryEstimateReading;

uint16_t battery_estimate_level_for(uint32_t mv);
void battery_estimate_update(BatteryEstimate *estimate, BatteryEstimateReading reading);
