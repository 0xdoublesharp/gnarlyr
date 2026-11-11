#pragma once

#include <stdint.h>

enum {
    USB_LEVEL_FULL = 255,
    USB_LEVEL_RANGE_DB = 48,
    USB_LEVEL_FALL_PER_SECOND = 400
};

uint8_t usb_level_of(uint16_t peak);
uint8_t usb_level_follow(uint8_t shown, uint8_t now, uint32_t elapsed_ms);
