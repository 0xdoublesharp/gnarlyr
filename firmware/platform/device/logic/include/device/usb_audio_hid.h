#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/key.h"

enum {
    USB_AUDIO_HID_QUEUE = 8,
    USB_AUDIO_HID_PLAY_PAUSE = 0xCD
};

typedef struct UsbAudioHid {
    uint8_t count;
    bool releasing;
    int8_t volume_step;
    uint32_t repeat_ms;
} UsbAudioHid;

int usb_audio_hid_key(UsbAudioHid *hid, Key key, bool pressed, uint32_t now_ms);
int usb_audio_hid_tick(UsbAudioHid *hid, uint32_t now_ms);
bool usb_audio_hid_report(const UsbAudioHid *hid, uint16_t *usage);
void usb_audio_hid_sent(UsbAudioHid *hid);
