#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    USB_FALLBACK_MS = 10000,
    USB_FALLBACK_COMMANDS = 2,
    USB_FALLBACK_RESETS = 6
};

typedef enum UsbFallbackCause {
    USB_FALLBACK_NONE,
    USB_FALLBACK_NOT_CONFIGURED,
    USB_FALLBACK_FEW_COMMANDS,
    USB_FALLBACK_RESETS_ONLY
} UsbFallbackCause;

typedef struct UsbFallbackSigns {
    bool configured;
    uint8_t resets;
    uint32_t commands;
} UsbFallbackSigns;

typedef struct UsbFallback {
    bool watching;
    bool timing;
    uint32_t started_ms;
    UsbFallbackCause cause;
    uint32_t commands;
} UsbFallback;

void usb_fallback_start(UsbFallback *fallback, bool high_speed);
void usb_fallback_enumerated(UsbFallback *fallback, uint32_t now_ms);
bool usb_fallback_due(UsbFallback *fallback, UsbFallbackSigns signs, uint32_t now_ms);
