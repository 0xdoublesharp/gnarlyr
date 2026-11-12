#include "device/usb_audio.h"

#include <stddef.h>

static const uint8_t DEVICE_DESCRIPTOR[] = {18,   1,    0, 2, 0, 0, 0, 64, 0x09,
                                            0x12, 0x02, 0, 0, 2, 1, 2, 0,  1};

static const uint8_t QUALIFIER[] = {10, 6, 0, 2, 0, 0, 0, 64, 1, 0};

static const uint8_t HID_REPORT[] = {0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x15, 0x00,
                                     0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
                                     0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0};

static const uint8_t FULL_CONFIGURATION[] = {
    9,    2,    184,  0,    3,    1,    0,    0x80, 50,   9,    4,    0,    0,    0,    1,    1,
    0,    0,    9,    0x24, 1,    0x00, 0x01, 40,   0,    1,    1,    12,   0x24, 2,    1,    0x01,
    0x01, 0,    2,    0x03, 0x00, 0,    0,    10,   0x24, 6,    2,    1,    1,    0x03, 0x00, 0x00,
    0,    9,    0x24, 3,    3,    0x02, 0x03, 0,    2,    0,    9,    4,    1,    0,    0,    1,
    2,    0,    0,    9,    4,    1,    1,    1,    1,    2,    0,    0,    7,    0x24, 1,    1,
    1,    0x01, 0x00, 14,   0x24, 2,    1,    2,    2,    16,   2,    0x44, 0xAC, 0x00, 0x80, 0xBB,
    0x00, 9,    5,    0x01, 0x09, 0xC4, 0x00, 1,    0,    0,    7,    0x25, 1,    0x01, 0,    0,
    0,    9,    4,    1,    2,    1,    1,    2,    0,    0,    7,    0x24, 1,    1,    1,    0x01,
    0x00, 14,   0x24, 2,    1,    2,    3,    24,   2,    0x44, 0xAC, 0x00, 0x80, 0xBB, 0x00, 9,
    5,    0x01, 0x09, 0x26, 0x01, 1,    0,    0,    7,    0x25, 1,    0x01, 0,    0,    0,    9,
    4,    2,    0,    1,    3,    0,    0,    0,    9,    0x21, 0x11, 0x01, 0,    1,    0x22, 23,
    0,    7,    5,    0x82, 3,    2,    0,    8};

#define STREAM_FORMAT(alt, width, packet)                                                          \
    9, 4, 1, alt, 2, 1, 2, 0x20, 0, 16, 0x24, 1, 1, 0, 1, 1, 0, 0, 0, 2, 3, 0, 0, 0, 0, 6, 0x24,   \
        2, 1, width, width * 8, 7, 5, 0x01, 0x05, packet, 0, 1, 8, 0x25, 1, 0, 0, 0, 0, 0, 7, 5,   \
        0x81, 0x11, 4, 0, 1

static const uint8_t HIGH_CONFIGURATION[] = {9,
                                             2,
                                             0x1B,
                                             1,
                                             3,
                                             1,
                                             0,
                                             0x80,
                                             50,
                                             8,
                                             0x0B,
                                             0,
                                             2,
                                             1,
                                             0,
                                             0x20,
                                             0,
                                             9,
                                             4,
                                             0,
                                             0,
                                             0,
                                             1,
                                             1,
                                             0x20,
                                             0,
                                             9,
                                             0x24,
                                             1,
                                             0,
                                             2,
                                             8,
                                             64,
                                             0,
                                             0,
                                             8,
                                             0x24,
                                             0x0A,
                                             4,
                                             3,
                                             7,
                                             0,
                                             0,
                                             17,
                                             0x24,
                                             2,
                                             1,
                                             1,
                                             1,
                                             0,
                                             4,
                                             2,
                                             3,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             18,
                                             0x24,
                                             6,
                                             2,
                                             1,
                                             0x0F,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             12,
                                             0x24,
                                             3,
                                             3,
                                             2,
                                             3,
                                             0,
                                             2,
                                             4,
                                             0,
                                             0,
                                             0,
                                             9,
                                             4,
                                             1,
                                             0,
                                             0,
                                             1,
                                             2,
                                             0x20,
                                             0,
                                             STREAM_FORMAT(1, 2, 100),
                                             STREAM_FORMAT(2, 3, 150),
                                             STREAM_FORMAT(3, 4, 200),
                                             9,
                                             4,
                                             2,
                                             0,
                                             1,
                                             3,
                                             0,
                                             0,
                                             0,
                                             9,
                                             0x21,
                                             0x11,
                                             1,
                                             0,
                                             1,
                                             0x22,
                                             sizeof HID_REPORT,
                                             0,
                                             7,
                                             5,
                                             0x82,
                                             3,
                                             2,
                                             0,
                                             7};

_Static_assert(sizeof FULL_CONFIGURATION == 184, "the UAC1 configuration is complete");
_Static_assert(sizeof HIGH_CONFIGURATION == 283, "the UAC2 configuration is complete");
_Static_assert(sizeof HIGH_CONFIGURATION <= USB_AUDIO_REPLY_BYTES, "descriptors fit the reply");

static UsbReply descriptor_data(const uint8_t *data, uint16_t length, UsbSetup setup)
{
    return (UsbReply){.kind = USB_REPLY_DATA,
                      .data = data,
                      .length = length < setup.length ? length : setup.length};
}

static void copy_bytes(uint8_t *target, const uint8_t *source, uint16_t length)
{
    for (uint16_t index = 0; index < length; index++) {
        target[index] = source[index];
    }
}

static UsbReply configuration(UsbAudio *audio, UsbSetup setup)
{
    bool high = audio->device->speed == USB_SPEED_HIGH;
    if ((setup.value >> 8) == 7) {
        high = !high;
    }
    const uint8_t *bytes = high ? HIGH_CONFIGURATION : FULL_CONFIGURATION;
    uint16_t length = high ? sizeof HIGH_CONFIGURATION : sizeof FULL_CONFIGURATION;
    copy_bytes(audio->reply, bytes, length);
    audio->reply[1] = (uint8_t)(setup.value >> 8);
    return descriptor_data(audio->reply, length, setup);
}

UsbReply usb_audio_descriptor(UsbAudio *audio, UsbSetup setup)
{
    uint8_t type = (uint8_t)(setup.value >> 8);
    if (setup.request_type == 0x81 && setup.index == USB_AUDIO_HID_INTERFACE) {
        if (type == 0x22) {
            return descriptor_data(HID_REPORT, sizeof HID_REPORT, setup);
        }
        if (type == 0x21) {
            return descriptor_data(HIGH_CONFIGURATION + sizeof HIGH_CONFIGURATION - 16, 9, setup);
        }
    }
    if (setup.request_type == 0x80 && (setup.value & 0xFF) == 0) {
        if (type == 2 || type == 7) {
            return configuration(audio, setup);
        }
        if (type == 1 || type == 6) {
            uint16_t length = type == 1 ? sizeof DEVICE_DESCRIPTOR : sizeof QUALIFIER;
            copy_bytes(audio->reply, type == 1 ? DEVICE_DESCRIPTOR : QUALIFIER, length);
            bool high = audio->device->speed == USB_SPEED_HIGH;
            if (type == 6) {
                high = !high;
            }
            audio->reply[4] = high ? 0xEF : 0;
            audio->reply[5] = high ? 2 : 0;
            audio->reply[6] = high ? 1 : 0;
            return descriptor_data(audio->reply, length, setup);
        }
    }
    return (UsbReply){.kind = USB_REPLY_STALL};
}
