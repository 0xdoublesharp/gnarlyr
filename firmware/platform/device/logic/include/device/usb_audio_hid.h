#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/key.h"

enum {
    USB_AUDIO_HID_QUEUE = 8,
    USB_AUDIO_HID_NEXT = 0xB5,
    USB_AUDIO_HID_PREVIOUS = 0xB6,
    USB_AUDIO_HID_PLAY_PAUSE = 0xCD,
    USB_AUDIO_HID_MUTE = 0xE2,
    USB_AUDIO_HID_VOLUME_UP = 0xE9,
    USB_AUDIO_HID_VOLUME_DOWN = 0xEA
};

typedef struct UsbAudioHid {
    uint8_t count;
    uint8_t head;
    bool releasing;
    int8_t volume_step;
    uint32_t repeat_ms;
    uint8_t queue[USB_AUDIO_HID_QUEUE];
} UsbAudioHid;

int usb_audio_hid_key(UsbAudioHid *hid, Key key, bool pressed, uint32_t now_ms);
int usb_audio_hid_tick(UsbAudioHid *hid, uint32_t now_ms);
bool usb_audio_hid_enqueue(UsbAudioHid *hid, uint8_t usage);
bool usb_audio_hid_report(const UsbAudioHid *hid, uint16_t *usage);
void usb_audio_hid_sent(UsbAudioHid *hid);
