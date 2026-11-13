#include "device/usb_fallback.h"

void usb_fallback_start(UsbFallback *fallback, bool high_speed)
{
    *fallback = (UsbFallback){.watching = high_speed, .cause = USB_FALLBACK_NONE};
}

void usb_fallback_enumerated(UsbFallback *fallback, uint32_t now_ms)
{
    if (fallback->watching && !fallback->timing) {
        fallback->timing = true;
        fallback->started_ms = now_ms;
    }
}

bool usb_fallback_due(UsbFallback *fallback, UsbFallbackSigns signs, uint32_t now_ms)
{
    if (!fallback->watching) {
        return false;
    }
    if (signs.configured && signs.commands >= USB_FALLBACK_COMMANDS) {
        fallback->watching = false;
        return false;
    }
    bool resets_only = !fallback->timing && signs.resets >= USB_FALLBACK_RESETS;
    if (!resets_only && (!fallback->timing || now_ms - fallback->started_ms < USB_FALLBACK_MS)) {
        return false;
    }
    fallback->watching = false;
    fallback->cause = resets_only        ? USB_FALLBACK_RESETS_ONLY
                      : signs.configured ? USB_FALLBACK_FEW_COMMANDS
                                         : USB_FALLBACK_NOT_CONFIGURED;
    fallback->commands = signs.commands;
    return true;
}
