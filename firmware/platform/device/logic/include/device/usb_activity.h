#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/msc.h"

enum {
    USB_ACTIVITY_SHOW_MS = 400,
    USB_ACTIVITY_QUIET_MS = 3000
};

typedef enum UsbActivityKind {
    USB_ACTIVITY_IDLE,
    USB_ACTIVITY_READING,
    USB_ACTIVITY_WRITING
} UsbActivityKind;

typedef struct UsbActivity {
    uint32_t read;
    uint32_t written;
    uint32_t flushes;
    uint32_t read_ms;
    uint32_t written_ms;
    bool has_read;
    bool has_written;
} UsbActivity;

void usb_activity_start(UsbActivity *activity, uint32_t now_ms);
void usb_activity_follow(UsbActivity *activity, const MscStats *stats, uint32_t now_ms);
UsbActivityKind usb_activity_now(const UsbActivity *activity, uint32_t now_ms);
bool usb_activity_quiet(const UsbActivity *activity, uint32_t now_ms);
uint32_t usb_activity_written_kib(const UsbActivity *activity);
