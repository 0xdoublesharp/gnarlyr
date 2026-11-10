#include "device/usb_plug.h"

static UsbPlugAction leave(UsbPlug *plug)
{
    UsbPlugAction action = plug->state == USB_PLUG_LISTENING ? USB_PLUG_STOP : USB_PLUG_NOTHING;
    plug->state = USB_PLUG_UNPLUGGED;
    return action;
}

UsbPlugAction usb_plug_update(UsbPlug *plug, bool cable, uint32_t now_ms)
{
    if (!cable) {
        return leave(plug);
    }
    if (plug->state == USB_PLUG_UNPLUGGED) {
        plug->state = USB_PLUG_SETTLING;
        plug->since_ms = now_ms;
    }
    if (plug->state == USB_PLUG_SETTLING && now_ms - plug->since_ms >= USB_PLUG_STEADY_MS) {
        return USB_PLUG_START;
    }
    return USB_PLUG_NOTHING;
}

void usb_plug_started(UsbPlug *plug, bool listening)
{
    plug->state = listening ? USB_PLUG_LISTENING : USB_PLUG_SERVED;
}

void usb_plug_served(UsbPlug *plug)
{
    plug->state = USB_PLUG_SERVED;
}

void usb_plug_retry(UsbPlug *plug)
{
    if (plug->state == USB_PLUG_SERVED) {
        plug->state = USB_PLUG_UNPLUGGED;
    }
}
