#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    USB_LEAVE_HOLD_MS = 1000,
    USB_LEAVE_FORCE_MS = 3000,
    USB_LEAVE_GRACE_MS = 2000
};

typedef enum UsbLeaveState {
    USB_LEAVE_SERVING,
    USB_LEAVE_WAITING,
    USB_LEAVE_REMOVING,
    USB_LEAVE_EJECTED,
    USB_LEAVE_FORCED
} UsbLeaveState;

typedef struct UsbLeave {
    uint8_t state;
    bool holding;
    uint32_t pressed_ms;
    uint32_t since_ms;
} UsbLeave;

typedef struct UsbLeaveNow {
    uint32_t now_ms;
    bool quiet;
    bool refused;
} UsbLeaveNow;

void usb_leave_key(UsbLeave *leave, bool pressed, uint32_t now_ms);
UsbLeaveState usb_leave_step(UsbLeave *leave, UsbLeaveNow now);
bool usb_leave_holding(const UsbLeave *leave);
