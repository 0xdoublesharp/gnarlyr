#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/usb_device.h"

enum {
    USB_AUDIO_ENDPOINT = 0x01,
    USB_AUDIO_FEEDBACK_ENDPOINT = 0x81,
    USB_AUDIO_HID_ENDPOINT = 0x82,
    USB_AUDIO_HID_INTERFACE = 2,
    USB_AUDIO_HIGH_MAX_PACKET = 200,
    USB_AUDIO_REPLY_BYTES = 284,
    USB_AUDIO_MAX_PACKET = 294,
    USB_AUDIO_DEFAULT_RATE = 48000,
    USB_AUDIO_CHANGED_FORMAT = 1,
    USB_AUDIO_CHANGED_VOLUME = 2,
    USB_AUDIO_MUTED = 0xFF
};

typedef struct UsbAudio {
    UsbDevice *device;
    uint8_t alt;
    bool muted;
    uint8_t changes;
    int16_t volume;
    uint32_t rate;
    uint8_t hid_idle;
    uint16_t hid_usage;
    uint8_t reply[USB_AUDIO_REPLY_BYTES];
} UsbAudio;

void usb_audio_init(UsbAudio *audio, UsbDevice *device);
void usb_audio_reset(UsbAudio *audio);
UsbReply usb_audio_setup(UsbAudio *audio, UsbSetup setup);
UsbReply usb_audio_receive(UsbAudio *audio, UsbSetup setup, const uint8_t *data, uint16_t length);
uint8_t usb_audio_take_changes(UsbAudio *audio);
uint8_t usb_audio_sample_bytes(const UsbAudio *audio);
uint8_t usb_audio_attenuation(const UsbAudio *audio);

UsbReply usb_audio_descriptor(UsbAudio *audio, UsbSetup setup);
bool usb_audio_asynchronous(const UsbAudio *audio);
uint16_t usb_audio_packet_size(const UsbAudio *audio);
