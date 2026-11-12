#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/key.h"

enum {
    USB_AUDIO_HID_QUEUE = 8,
    USB_AUDIO_HID_PLAY_PAUSE = 0xCD,
    USB_AUDIO_HID_NEXT = 0xB5,
    USB_AUDIO_HID_PREVIOUS = 0xB6
};

typedef struct UsbAudioHid {
    uint16_t queue[USB_AUDIO_HID_QUEUE];
    uint8_t head;
    uint8_t count;
    bool releasing;
    uint8_t held;
    uint8_t skipped;
    uint32_t pressed_ms[2];
} UsbAudioHid;

int usb_audio_hid_key(UsbAudioHid *hid, Key key, bool pressed, uint32_t now_ms);
void usb_audio_hid_tick(UsbAudioHid *hid, uint32_t now_ms);
bool usb_audio_hid_report(const UsbAudioHid *hid, uint16_t *usage);
void usb_audio_hid_sent(UsbAudioHid *hid);
