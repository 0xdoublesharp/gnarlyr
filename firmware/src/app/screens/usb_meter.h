#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    USB_METER_SAMPLES = 20,
    USB_METER_PERIOD_MS = 500,
    USB_METER_SECTOR_BYTES = 512,
    USB_METER_RATE_LIMIT = 0xFFFF,
    USB_METER_GLOW_MS = 300,
    USB_METER_LIT_MS = 400,
    USB_METER_SMOOTH_SHARE = 2,
    USB_METER_GLOW_FULL = 255,
    USB_METER_HOLD_MS = 1000,
    USB_METER_QUIET_MS = 2000
};

typedef enum UsbMeterDirection {
    USB_METER_READ,
    USB_METER_WRITE,
    USB_METER_DIRECTIONS
} UsbMeterDirection;

typedef struct UsbMeter {
    uint16_t history[USB_METER_DIRECTIONS][USB_METER_SAMPLES];
    uint32_t sectors[USB_METER_DIRECTIONS];
    uint32_t sampled_ms;
    uint32_t active_ms;
    uint32_t peak;
    uint8_t newest;
    uint8_t count;
} UsbMeter;

typedef struct UsbSteady {
    uint8_t shown;
    uint32_t since_ms;
} UsbSteady;

void usb_meter_start(UsbMeter *meter, uint32_t now_ms);
bool usb_meter_due(const UsbMeter *meter, uint32_t now_ms);
void usb_meter_sample(UsbMeter *meter, const uint32_t *sectors, uint32_t now_ms);
uint32_t usb_meter_rate(const UsbMeter *meter, UsbMeterDirection direction, int age);
uint32_t usb_meter_total(const UsbMeter *meter, int age);
uint32_t usb_meter_average(const UsbMeter *meter);
bool usb_meter_moving(const UsbMeter *meter);
uint8_t usb_meter_glow(bool seen, uint32_t age_ms);
bool usb_meter_lit(bool seen, uint32_t age_ms);
uint32_t usb_meter_smooth(uint32_t shown, uint32_t sample);
void usb_steady_start(UsbSteady *steady, uint8_t shown, uint32_t now_ms);
bool usb_steady_follow(UsbSteady *steady, uint8_t wanted, uint32_t now_ms);
