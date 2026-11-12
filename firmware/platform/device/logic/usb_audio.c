#include "device/usb_audio.h"

#include <stddef.h>

enum {
    TYPE_STANDARD_DEVICE_IN = 0x80,
    TYPE_STANDARD_INTERFACE_IN = 0x81,
    TYPE_STANDARD_INTERFACE_OUT = 0x01,
    TYPE_CLASS_INTERFACE_OUT = 0x21,
    TYPE_CLASS_ENDPOINT_OUT = 0x22,
    TYPE_CLASS_INTERFACE_IN = 0xA1,
    TYPE_CLASS_ENDPOINT_IN = 0xA2,
    RECIPIENT_MASK = 0x1F,
    RECIPIENT_INTERFACE = 1,
    RECIPIENT_ENDPOINT = 2,
    REQUEST_GET_DESCRIPTOR = 6,
    REQUEST_GET_INTERFACE = 10,
    REQUEST_SET_INTERFACE = 11,
    REQUEST_SET_CUR = 0x01,
    REQUEST_GET_CUR = 0x81,
    REQUEST_GET_MIN = 0x82,
    REQUEST_GET_MAX = 0x83,
    REQUEST_GET_RES = 0x84,
    DESCRIPTOR_DEVICE = 1,
    DESCRIPTOR_CONFIGURATION = 2,
    STREAMING_INTERFACE = 1,
    LAST_ALT = 2,
    FEATURE_UNIT_TARGET = 0x0200,
    SELECTOR_MUTE = 1,
    SELECTOR_VOLUME = 2,
    SELECTOR_SAMPLING_FREQUENCY = 1,
    VOLUME_MIN = -16384,
    VOLUME_MAX = 0,
    VOLUME_RESOLUTION = 128,
    RATE_BYTES = 3
};

typedef enum AudioControl {
    CONTROL_NONE,
    CONTROL_MUTE,
    CONTROL_VOLUME,
    CONTROL_RATE,
    CONTROL_VALID
} AudioControl;

static const uint32_t RATES[] = {44100, 48000, 88200, 96000, 176400, 192000};

void usb_audio_init(UsbAudio *audio, UsbDevice *device)
{
    usb_device_init(device, "", 1);
    usb_device_allow_high_speed(device, true);
    *audio = (UsbAudio){.device = device, .rate = USB_AUDIO_DEFAULT_RATE};
}

static void set_alt(UsbAudio *audio, uint8_t alt)
{
    if (audio->alt != alt) {
        audio->alt = alt;
        audio->changes |= USB_AUDIO_CHANGED_FORMAT;
    }
}

void usb_audio_reset(UsbAudio *audio)
{
    set_alt(audio, 0);
    audio->rate = USB_AUDIO_DEFAULT_RATE;
    audio->hid_idle = 0;
    audio->hid_usage = 0;
    audio->changes |= USB_AUDIO_CHANGED_FORMAT;
}

static UsbReply stall(void)
{
    return (UsbReply){USB_REPLY_STALL, NULL, 0, USB_ACTION_NONE, 0};
}

static UsbReply status(UsbAction action, uint16_t value)
{
    return (UsbReply){USB_REPLY_STATUS, NULL, 0, action, value};
}

static UsbReply data_reply(const uint8_t *bytes, uint16_t length, UsbSetup setup)
{
    uint16_t sent = length < setup.length ? length : setup.length;
    return (UsbReply){USB_REPLY_DATA, bytes, sent, USB_ACTION_NONE, 0};
}

static UsbReply little_endian(UsbAudio *audio, uint32_t value, uint16_t length, UsbSetup setup)
{
    for (uint16_t index = 0; index < length; index++) {
        audio->reply[index] = (uint8_t)(value >> (8 * index));
    }
    return data_reply(audio->reply, length, setup);
}

static bool rate_supported(const UsbAudio *audio, uint32_t rate)
{
    for (size_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
        if (RATES[index] == rate && (index < 2 || usb_audio_asynchronous(audio))) {
            return true;
        }
    }
    return false;
}

static AudioControl control_of(const UsbAudio *audio, UsbSetup setup)
{
    uint8_t selector = (uint8_t)(setup.value >> 8);
    uint8_t recipient = setup.request_type & RECIPIENT_MASK;
    if (usb_audio_asynchronous(audio) && recipient == RECIPIENT_INTERFACE &&
        setup.index == 0x0400 && (setup.value & 0xFF) == 0) {
        return selector == 1 ? CONTROL_RATE : selector == 2 ? CONTROL_VALID : CONTROL_NONE;
    }
    if (recipient == RECIPIENT_ENDPOINT && !usb_audio_asynchronous(audio)) {
        bool rate =
            setup.index == USB_AUDIO_ENDPOINT && setup.value == (SELECTOR_SAMPLING_FREQUENCY << 8);
        return rate ? CONTROL_RATE : CONTROL_NONE;
    }
    if (recipient != RECIPIENT_INTERFACE || setup.index != FEATURE_UNIT_TARGET ||
        (setup.value & 0xFF) != 0) {
        return CONTROL_NONE;
    }
    return selector == SELECTOR_MUTE     ? CONTROL_MUTE
           : selector == SELECTOR_VOLUME ? CONTROL_VOLUME
                                         : CONTROL_NONE;
}

static UsbReply volume_reply(UsbAudio *audio, UsbSetup setup)
{
    switch (setup.request) {
    case REQUEST_GET_CUR:
        return little_endian(audio, (uint16_t)audio->volume, 2, setup);
    case REQUEST_GET_MIN:
        return little_endian(audio, (uint16_t)VOLUME_MIN, 2, setup);
    case REQUEST_GET_MAX:
        return little_endian(audio, VOLUME_MAX, 2, setup);
    case REQUEST_GET_RES:
        return little_endian(audio, VOLUME_RESOLUTION, 2, setup);
    default:
        return stall();
    }
}

static UsbReply class_in(UsbAudio *audio, UsbSetup setup)
{
    AudioControl control = control_of(audio, setup);
    if (control == CONTROL_VOLUME) {
        return volume_reply(audio, setup);
    }
    if (setup.request != REQUEST_GET_CUR) {
        return stall();
    }
    if (control == CONTROL_MUTE) {
        return little_endian(audio, audio->muted, 1, setup);
    }
    return control == CONTROL_RATE ? little_endian(audio, audio->rate, RATE_BYTES, setup) : stall();
}

static UsbReply class_out(UsbAudio *audio, UsbSetup setup)
{
    AudioControl control = control_of(audio, setup);
    uint16_t length = control == CONTROL_RATE     ? (usb_audio_asynchronous(audio) ? 4 : 3)
                      : control == CONTROL_VOLUME ? 2
                                                  : 1;
    bool known = setup.request == REQUEST_SET_CUR && setup.length == length &&
                 control != CONTROL_NONE && control != CONTROL_VALID;
    return known ? (UsbReply){USB_REPLY_RECEIVE, NULL, setup.length, USB_ACTION_NONE, 0} : stall();
}

static UsbReply set_interface(UsbAudio *audio, UsbSetup setup)
{
    if (setup.length != 0) {
        return stall();
    }
    if (setup.index == STREAMING_INTERFACE &&
        setup.value <= (usb_audio_asynchronous(audio) ? 3 : LAST_ALT)) {
        set_alt(audio, (uint8_t)setup.value);
        return status(USB_ACTION_STREAM, setup.value);
    }
    return (setup.index == 0 || setup.index == USB_AUDIO_HID_INTERFACE) && setup.value == 0
               ? status(USB_ACTION_NONE, 0)
               : stall();
}

static bool own_descriptor(UsbSetup setup)
{
    uint8_t type = (uint8_t)(setup.value >> 8);
    return setup.request_type == TYPE_STANDARD_DEVICE_IN &&
           setup.request == REQUEST_GET_DESCRIPTOR &&
           (type == DESCRIPTOR_DEVICE || type == DESCRIPTOR_CONFIGURATION || type == 6 ||
            type == 7);
}

static UsbReply standard(UsbAudio *audio, UsbSetup setup)
{
    UsbReply reply = usb_device_setup(audio->device, setup);
    if (reply.action == USB_ACTION_CONFIGURE) {
        set_alt(audio, 0);
    }
    return reply;
}

static void put_value(uint8_t *bytes, uint32_t value, uint8_t length)
{
    for (uint8_t index = 0; index < length; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

static UsbReply range_reply(UsbAudio *audio, UsbSetup setup, AudioControl control)
{
    if (control == CONTROL_RATE) {
        put_value(audio->reply, 6, 2);
        for (uint8_t index = 0; index < 6; index++) {
            put_value(audio->reply + 2 + index * 12, RATES[index], 4);
            put_value(audio->reply + 6 + index * 12, RATES[index], 4);
            put_value(audio->reply + 10 + index * 12, 0, 4);
        }
        return data_reply(audio->reply, 74, setup);
    }
    if (control != CONTROL_VOLUME) {
        return stall();
    }
    put_value(audio->reply, 1, 2);
    put_value(audio->reply + 2, (uint16_t)VOLUME_MIN, 2);
    put_value(audio->reply + 4, VOLUME_MAX, 2);
    put_value(audio->reply + 6, VOLUME_RESOLUTION, 2);
    return data_reply(audio->reply, 8, setup);
}

static UsbReply uac2_in(UsbAudio *audio, UsbSetup setup)
{
    AudioControl control = control_of(audio, setup);
    if (setup.request == 2) {
        return range_reply(audio, setup, control);
    }
    if (setup.request != 1) {
        return stall();
    }
    if (control == CONTROL_VALID) {
        return little_endian(audio, 1, 1, setup);
    }
    if (control == CONTROL_RATE) {
        return little_endian(audio, audio->rate, 4, setup);
    }
    setup.request = REQUEST_GET_CUR;
    return class_in(audio, setup);
}

static UsbReply hid_setup(UsbAudio *audio, UsbSetup setup)
{
    if (setup.request_type == TYPE_CLASS_INTERFACE_IN && setup.request == 1 &&
        setup.value == 0x0100) {
        return little_endian(audio, audio->hid_usage, 2, setup);
    }
    if (setup.request_type == TYPE_CLASS_INTERFACE_IN && setup.request == 2 && setup.value == 0) {
        return little_endian(audio, audio->hid_idle, 1, setup);
    }
    if (setup.request_type == TYPE_CLASS_INTERFACE_OUT && setup.request == 10 &&
        (setup.value & 0xFF) == 0 && setup.length == 0) {
        audio->hid_idle = (uint8_t)(setup.value >> 8);
        return status(USB_ACTION_NONE, 0);
    }
    return stall();
}

UsbReply usb_audio_setup(UsbAudio *audio, UsbSetup setup)
{
    if (setup.request_type == TYPE_STANDARD_INTERFACE_IN &&
        setup.request == REQUEST_GET_DESCRIPTOR) {
        return usb_audio_descriptor(audio, setup);
    }
    if (setup.index == USB_AUDIO_HID_INTERFACE &&
        (setup.request_type == TYPE_CLASS_INTERFACE_IN ||
         setup.request_type == TYPE_CLASS_INTERFACE_OUT)) {
        return hid_setup(audio, setup);
    }
    if (own_descriptor(setup)) {
        return usb_audio_descriptor(audio, setup);
    }
    switch (setup.request_type) {
    case TYPE_CLASS_INTERFACE_IN:
    case TYPE_CLASS_ENDPOINT_IN:
        return usb_audio_asynchronous(audio) ? uac2_in(audio, setup) : class_in(audio, setup);
    case TYPE_CLASS_INTERFACE_OUT:
    case TYPE_CLASS_ENDPOINT_OUT:
        return class_out(audio, setup);
    case TYPE_STANDARD_INTERFACE_IN:
        return setup.request == REQUEST_GET_INTERFACE
                   ? little_endian(audio, setup.index == STREAMING_INTERFACE ? audio->alt : 0, 1,
                                   setup)
                   : standard(audio, setup);
    case TYPE_STANDARD_INTERFACE_OUT:
        return setup.request == REQUEST_SET_INTERFACE ? set_interface(audio, setup)
                                                      : standard(audio, setup);
    default:
        return standard(audio, setup);
    }
}

static int16_t clamped_volume(int32_t volume)
{
    return (int16_t)(volume < VOLUME_MIN ? VOLUME_MIN : volume > VOLUME_MAX ? VOLUME_MAX : volume);
}

static bool take_rate(UsbAudio *audio, uint32_t rate)
{
    if (!rate_supported(audio, rate)) {
        return false;
    }
    if (audio->rate != rate) {
        audio->rate = rate;
        audio->changes |= USB_AUDIO_CHANGED_FORMAT;
    }
    return true;
}

static uint32_t read_little_endian(const uint8_t *data, uint16_t length)
{
    uint32_t value = 0;
    for (uint16_t index = length; index > 0; index--) {
        value = value << 8 | data[index - 1];
    }
    return value;
}

UsbReply usb_audio_receive(UsbAudio *audio, UsbSetup setup, const uint8_t *data, uint16_t length)
{
    AudioControl control = control_of(audio, setup);
    uint16_t needed =
        (uint16_t)(control == CONTROL_RATE     ? (usb_audio_asynchronous(audio) ? 4 : RATE_BYTES)
                   : control == CONTROL_VOLUME ? 2
                                               : 1);
    if (class_out(audio, setup).kind != USB_REPLY_RECEIVE || length != needed) {
        return stall();
    }
    uint32_t value = read_little_endian(data, needed);
    if (control == CONTROL_RATE) {
        return take_rate(audio, value) ? status(USB_ACTION_NONE, 0) : stall();
    }
    if (control == CONTROL_MUTE) {
        audio->muted = value != 0;
    } else {
        audio->volume = clamped_volume((int16_t)value);
    }
    audio->changes |= USB_AUDIO_CHANGED_VOLUME;
    return status(USB_ACTION_NONE, 0);
}

uint8_t usb_audio_take_changes(UsbAudio *audio)
{
    uint8_t changes = audio->changes;
    audio->changes = 0;
    return changes;
}

uint8_t usb_audio_sample_bytes(const UsbAudio *audio)
{
    return audio->alt == 0 ? 0 : (uint8_t)(audio->alt + 1);
}

uint8_t usb_audio_attenuation(const UsbAudio *audio)
{
    return audio->muted ? USB_AUDIO_MUTED : (uint8_t)(-audio->volume / VOLUME_RESOLUTION);
}

bool usb_audio_asynchronous(const UsbAudio *audio)
{
    return audio->device->speed == USB_SPEED_HIGH;
}

uint16_t usb_audio_packet_size(const UsbAudio *audio)
{
    return usb_audio_asynchronous(audio) ? (uint16_t)(50 * usb_audio_sample_bytes(audio))
                                         : USB_AUDIO_MAX_PACKET;
}
