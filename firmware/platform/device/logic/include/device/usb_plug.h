#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum UsbPlugState {
    USB_PLUG_UNPLUGGED,
    USB_PLUG_SETTLING,
    USB_PLUG_LISTENING,
    USB_PLUG_SERVED
} UsbPlugState;

typedef enum UsbPlugAction {
    USB_PLUG_NOTHING,
    USB_PLUG_START,
    USB_PLUG_STOP
} UsbPlugAction;

enum {
    USB_PLUG_STEADY_MS = 1000,
    USB_PLUG_HOST_WAIT_MS = 3000
};

typedef struct UsbPlug {
    UsbPlugState state;
    uint32_t since_ms;
} UsbPlug;

UsbPlugAction usb_plug_update(UsbPlug *plug, bool cable, uint32_t now_ms);
void usb_plug_started(UsbPlug *plug, bool listening, uint32_t now_ms);
void usb_plug_served(UsbPlug *plug);
void usb_plug_retry(UsbPlug *plug);
