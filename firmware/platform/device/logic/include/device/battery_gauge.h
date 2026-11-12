#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    BATTERY_GAUGE_READINGS = 64,
    BATTERY_DISPLAY_STEP = 2,
    BATTERY_DISPLAY_REVERSE_STEP = 10,
    BATTERY_DISPLAY_LOAD_STEP = 3,
    BATTERY_BOOT_CUTOFF_MILLIVOLTS = 3400,
    BATTERY_CHARGING_MAX_PERCENT = 99,
    BATTERY_FULL_PERCENT = 100,
    BATTERY_RECENT_READINGS = 8,
    CHARGE_SETTLE_MS = 5000,
    CHARGE_FULL_HOLD_MS = 2000,
    CHARGE_FULL_MIN_MILLIVOLTS = 4050
};

typedef struct BatteryGauge {
    uint32_t millivolts[BATTERY_GAUGE_READINGS];
    uint32_t count;
    uint32_t next;
} BatteryGauge;

uint32_t battery_millivolts(uint16_t raw);
uint8_t battery_percent_for(uint32_t millivolts);
bool battery_boot_cutoff(uint32_t millivolts, bool usb_power);
void battery_gauge_init(BatteryGauge *gauge);
void battery_gauge_add(BatteryGauge *gauge, uint16_t raw);
void battery_gauge_restart(BatteryGauge *gauge);
bool battery_gauge_known(const BatteryGauge *gauge);
uint32_t battery_gauge_millivolts(const BatteryGauge *gauge);
uint32_t battery_gauge_recent_millivolts(const BatteryGauge *gauge);
uint8_t battery_gauge_percent(const BatteryGauge *gauge, bool charging);

typedef struct BatteryDisplay {
    bool shown_known;
    uint8_t shown;
} BatteryDisplay;

void battery_display_init(BatteryDisplay *display);
uint8_t battery_display_update(BatteryDisplay *display, uint8_t percent, bool usb_power);
void battery_display_show_full(BatteryDisplay *display);

typedef enum ChargeState {
    CHARGE_ON_BATTERY,
    CHARGE_CHARGING,
    CHARGE_FULL
} ChargeState;

typedef struct ChargeReading {
    bool usb_power;
    bool status_high;
    uint32_t millivolts;
    uint32_t now_ms;
} ChargeReading;

typedef struct ChargeTracker {
    bool usb_power;
    bool high_seen;
    bool full;
    uint32_t usb_since_ms;
    uint32_t high_since_ms;
} ChargeTracker;

void charge_tracker_init(ChargeTracker *tracker);
ChargeState charge_tracker_update(ChargeTracker *tracker, ChargeReading reading);

typedef struct BatteryCareReading {
    bool usb_power;
    bool known;
    uint16_t millivolts;
    uint8_t mode;
} BatteryCareReading;

bool battery_care_charge(bool charging, BatteryCareReading reading);
