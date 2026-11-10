#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    USB_FORCED_IDLE_MS = 60000
};

typedef struct UsbForced {
    bool cable_seen;
    bool heard;
    uint32_t commands;
    uint32_t heard_ms;
} UsbForced;

typedef struct UsbForcedNow {
    bool cable;
    bool requested;
    uint32_t commands;
    uint32_t now_ms;
} UsbForcedNow;

bool usb_forced_connected(UsbForced *forced, UsbForcedNow now);
