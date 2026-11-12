#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct HalBattery {
    bool known;
    uint8_t percent;
    bool charging;
    bool usb_power;
    bool charge_disabled;
    uint16_t millivolts;
} HalBattery;

HalBattery hal_power_battery(void);

typedef struct HalPowerHold {
    uint8_t function;
    bool output;
    bool high;
    bool usb_power;
    bool key_down;
} HalPowerHold;

HalPowerHold hal_power_boot_hold(void);
bool hal_power_take_usb_start(void);
bool hal_power_cable(void);
void hal_power_off(void);

bool hal_power_usb_host(void);
void hal_power_set_battery_care(uint8_t mode);
void hal_power_use_battery_care(uint8_t mode);
uint8_t hal_power_battery_care(void);
