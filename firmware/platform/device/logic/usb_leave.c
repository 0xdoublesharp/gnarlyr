#include "device/usb_leave.h"

void usb_leave_key(UsbLeave *leave, bool pressed, uint32_t now_ms)
{
    leave->holding = pressed;
    if (pressed) {
        leave->pressed_ms = now_ms;
    }
}

bool usb_leave_holding(const UsbLeave *leave)
{
    return leave->holding;
}

static void enter(UsbLeave *leave, UsbLeaveState state, uint32_t now_ms)
{
    leave->state = (uint8_t)state;
    leave->since_ms = now_ms;
}

static bool held_for(const UsbLeave *leave, uint32_t from_ms, uint32_t span_ms, uint32_t now_ms)
{
    return leave->holding && now_ms - from_ms >= span_ms;
}

static uint32_t force_start(const UsbLeave *leave)
{
    return leave->since_ms - leave->pressed_ms < 0x80000000u ? leave->since_ms : leave->pressed_ms;
}

static void serving(UsbLeave *leave, UsbLeaveNow now)
{
    if (held_for(leave, leave->pressed_ms, USB_LEAVE_HOLD_MS, now.now_ms)) {
        enter(leave, now.quiet ? USB_LEAVE_REMOVING : USB_LEAVE_WAITING, now.now_ms);
    }
}

static void waiting(UsbLeave *leave, UsbLeaveNow now)
{
    if (now.quiet) {
        enter(leave, USB_LEAVE_REMOVING, now.now_ms);
    } else if (held_for(leave, force_start(leave), USB_LEAVE_FORCE_MS, now.now_ms)) {
        enter(leave, USB_LEAVE_FORCED, now.now_ms);
    }
}

static void removing(UsbLeave *leave, UsbLeaveNow now)
{
    if (now.refused) {
        enter(leave, USB_LEAVE_FORCED, now.now_ms);
    } else if (now.now_ms - leave->since_ms >= USB_LEAVE_GRACE_MS) {
        enter(leave, USB_LEAVE_EJECTED, now.now_ms);
    }
}

UsbLeaveState usb_leave_step(UsbLeave *leave, UsbLeaveNow now)
{
    switch (leave->state) {
    case USB_LEAVE_SERVING:
        serving(leave, now);
        break;
    case USB_LEAVE_WAITING:
        waiting(leave, now);
        break;
    case USB_LEAVE_REMOVING:
        removing(leave, now);
        break;
    default:
        break;
    }
    return (UsbLeaveState)leave->state;
}
