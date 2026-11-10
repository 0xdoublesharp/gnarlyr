#include "device/usb_forced.h"

static bool host_still_talks(UsbForced *forced, UsbForcedNow now)
{
    if (!forced->heard || now.commands != forced->commands) {
        forced->heard = true;
        forced->commands = now.commands;
        forced->heard_ms = now.now_ms;
    }
    return now.now_ms - forced->heard_ms < USB_FORCED_IDLE_MS;
}

bool usb_forced_connected(UsbForced *forced, UsbForcedNow now)
{
    if (now.cable) {
        forced->cable_seen = true;
        return true;
    }
    if (!now.requested || forced->cable_seen) {
        return false;
    }
    return host_still_talks(forced, now);
}
