#include "usb_meter.h"

void usb_meter_start(UsbMeter *meter, uint32_t now_ms)
{
    *meter = (UsbMeter){.sampled_ms = now_ms};
}

bool usb_meter_due(const UsbMeter *meter, uint32_t now_ms)
{
    return now_ms - meter->sampled_ms >= USB_METER_PERIOD_MS;
}

static uint32_t rate_of(uint32_t sectors, uint32_t elapsed_ms)
{
    uint64_t rate = (uint64_t)sectors * USB_METER_SECTOR_BYTES / elapsed_ms;
    return rate < USB_METER_RATE_LIMIT ? (uint32_t)rate : USB_METER_RATE_LIMIT;
}

void usb_meter_sample(UsbMeter *meter, const uint32_t *sectors, uint32_t now_ms)
{
    uint32_t elapsed = now_ms - meter->sampled_ms;
    if (elapsed == 0) {
        return;
    }
    meter->newest = (uint8_t)((meter->newest + 1) % USB_METER_SAMPLES);
    meter->count = meter->count < USB_METER_SAMPLES ? (uint8_t)(meter->count + 1) : meter->count;
    bool moved = false;
    for (int direction = 0; direction < USB_METER_DIRECTIONS; direction++) {
        uint32_t delta = sectors[direction] - meter->sectors[direction];
        meter->history[direction][meter->newest] = (uint16_t)rate_of(delta, elapsed);
        meter->sectors[direction] = sectors[direction];
        moved = moved || delta != 0;
    }
    meter->active_ms += moved ? elapsed : 0;
    uint32_t total = usb_meter_total(meter, 0);
    meter->peak = total > meter->peak ? total : meter->peak;
    meter->sampled_ms = now_ms;
}

uint32_t usb_meter_rate(const UsbMeter *meter, UsbMeterDirection direction, int age)
{
    if (age < 0 || age >= meter->count) {
        return 0;
    }
    int slot = (meter->newest + USB_METER_SAMPLES - age) % USB_METER_SAMPLES;
    return meter->history[direction][slot];
}

uint32_t usb_meter_total(const UsbMeter *meter, int age)
{
    return usb_meter_rate(meter, USB_METER_READ, age) + usb_meter_rate(meter, USB_METER_WRITE, age);
}

uint32_t usb_meter_average(const UsbMeter *meter)
{
    if (meter->active_ms == 0) {
        return 0;
    }
    uint64_t sectors = (uint64_t)meter->sectors[USB_METER_READ] + meter->sectors[USB_METER_WRITE];
    uint64_t rate = sectors * USB_METER_SECTOR_BYTES / meter->active_ms;
    return rate < USB_METER_RATE_LIMIT ? (uint32_t)rate : USB_METER_RATE_LIMIT;
}

bool usb_meter_moving(const UsbMeter *meter)
{
    for (int age = 0; age < meter->count; age++) {
        if (usb_meter_total(meter, age) != 0) {
            return true;
        }
    }
    return false;
}

uint8_t usb_meter_glow(bool seen, uint32_t age_ms)
{
    if (!seen || age_ms >= USB_METER_GLOW_MS) {
        return 0;
    }
    uint32_t left = USB_METER_GLOW_MS - age_ms;
    return (uint8_t)((USB_METER_GLOW_FULL * left + USB_METER_GLOW_MS - 1) / USB_METER_GLOW_MS);
}

void usb_steady_start(UsbSteady *steady, uint8_t shown, uint32_t now_ms)
{
    *steady = (UsbSteady){.shown = shown, .since_ms = now_ms - USB_METER_HOLD_MS};
}

bool usb_steady_follow(UsbSteady *steady, uint8_t wanted, uint32_t now_ms)
{
    if (wanted == steady->shown || now_ms - steady->since_ms < USB_METER_HOLD_MS) {
        return false;
    }
    steady->shown = wanted;
    steady->since_ms = now_ms;
    return true;
}
