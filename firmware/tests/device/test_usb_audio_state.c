#include <string.h>

#include "check.h"
#include "device/usb_audio.h"

static const uint32_t RATES[] = {44100, 48000, 88200, 96000, 176400, 192000, 12345};

typedef struct UsbModel {
    uint32_t rate;
    int16_t volume;
    uint8_t alt;
    uint8_t changes;
    bool muted;
    bool high;
} UsbModel;

static uint32_t next_random(uint32_t *seed)
{
    uint32_t value = *seed;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return *seed = value;
}

static uint32_t response_value(UsbReply reply, uint16_t length)
{
    CHECK_EQUAL(USB_REPLY_DATA, reply.kind);
    CHECK_EQUAL(length, reply.length);
    uint32_t value = 0;
    for (uint16_t byte = length; byte > 0; byte--) {
        value = value << 8 | reply.data[byte - 1];
    }
    return value;
}

static UsbSetup rate_setup(const UsbModel *model)
{
    return model->high ? (UsbSetup){0x21, 1, 0x0100, 0x0400, 4}
                       : (UsbSetup){0x22, 1, 0x0100, USB_AUDIO_ENDPOINT, 3};
}

static UsbReply write_control(UsbAudio *audio, UsbSetup setup, uint32_t value, uint16_t length)
{
    const uint8_t bytes[] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                             (uint8_t)(value >> 24)};
    CHECK_EQUAL(USB_REPLY_RECEIVE, usb_audio_setup(audio, setup).kind);
    return usb_audio_receive(audio, setup, bytes, length);
}

static void select_alt(UsbAudio *audio, UsbModel *model, uint8_t alt)
{
    UsbReply reply = usb_audio_setup(audio, (UsbSetup){0x01, 11, alt, 1, 0});
    if (alt > (model->high ? 3 : 2)) {
        CHECK_EQUAL(USB_REPLY_STALL, reply.kind);
        return;
    }
    CHECK_EQUAL(USB_REPLY_STATUS, reply.kind);
    CHECK_EQUAL(USB_ACTION_STREAM, reply.action);
    CHECK_EQUAL(alt, reply.value);
    if (model->alt != alt) {
        model->changes |= USB_AUDIO_CHANGED_FORMAT;
    }
    model->alt = alt;
}

static void write_rate(UsbAudio *audio, UsbModel *model, uint32_t choice)
{
    UsbSetup setup = rate_setup(model);
    uint32_t rate = RATES[choice];
    UsbReply reply = write_control(audio, setup, rate, setup.length);
    if (choice >= (model->high ? 6u : 2u)) {
        CHECK_EQUAL(USB_REPLY_STALL, reply.kind);
        return;
    }
    CHECK_EQUAL(USB_REPLY_STATUS, reply.kind);
    CHECK_EQUAL(model->alt ? USB_ACTION_STREAM : USB_ACTION_NONE, reply.action);
    CHECK_EQUAL(model->alt, reply.value);
    if (model->rate != rate) {
        model->changes |= USB_AUDIO_CHANGED_FORMAT;
    }
    model->rate = rate;
}

static void write_volume(UsbAudio *audio, UsbModel *model, uint32_t choice)
{
    static const int16_t VOLUMES[] = {-32768, -16384, -4096, -128, 0, 32767};
    int16_t value = VOLUMES[choice % TEST_CASE_COUNT(VOLUMES)];
    UsbReply reply =
        write_control(audio, (UsbSetup){0x21, 1, 0x0200, 0x0200, 2}, (uint16_t)value, 2);
    CHECK_EQUAL(USB_REPLY_STATUS, reply.kind);
    CHECK_EQUAL(USB_ACTION_NONE, reply.action);
    model->volume = value < -16384 ? -16384 : value > 0 ? 0 : value;
    model->changes |= USB_AUDIO_CHANGED_VOLUME;
}

static void write_mute(UsbAudio *audio, UsbModel *model, bool mute)
{
    UsbReply reply = write_control(audio, (UsbSetup){0x21, 1, 0x0100, 0x0200, 1}, mute, 1);
    CHECK_EQUAL(USB_REPLY_STATUS, reply.kind);
    CHECK_EQUAL(USB_ACTION_NONE, reply.action);
    model->muted = mute;
    model->changes |= USB_AUDIO_CHANGED_VOLUME;
}

static void read_range(UsbAudio *audio, const UsbModel *model)
{
    UsbSetup setup = rate_setup(model);
    setup.request_type |= 0x80;
    setup.request = 2;
    setup.length = 74;
    UsbReply reply = usb_audio_setup(audio, setup);
    if (!model->high) {
        CHECK_EQUAL(USB_REPLY_STALL, reply.kind);
        return;
    }
    CHECK_EQUAL(USB_REPLY_DATA, reply.kind);
    CHECK_EQUAL(74, reply.length);
    CHECK_EQUAL(6,
                response_value((UsbReply){.kind = reply.kind, .data = reply.data, .length = 2}, 2));
    for (uint32_t index = 0; index < 6; index++) {
        const uint8_t *data = reply.data + 2 + 12 * index;
        CHECK_EQUAL(RATES[index],
                    response_value((UsbReply){.kind = reply.kind, .data = data, .length = 4}, 4));
        CHECK(memcmp(data, data + 4, 4) == 0);
        CHECK_EQUAL(
            0, response_value((UsbReply){.kind = reply.kind, .data = data + 8, .length = 4}, 4));
    }
}

static void reject_malformed(UsbAudio *audio, const UsbModel *model, uint32_t choice)
{
    UsbSetup setup = rate_setup(model);
    switch (choice % 6) {
    case 0:
        CHECK_EQUAL(USB_REPLY_STALL, write_control(audio, setup, 44100, setup.length - 1).kind);
        break;
    case 1:
        setup.index ^= 0x8000;
        CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(audio, setup).kind);
        break;
    case 2:
        CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(audio, (UsbSetup){0x01, 11, 1, 1, 1}).kind);
        break;
    case 3:
        setup.length++;
        CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(audio, setup).kind);
        break;
    case 4:
        CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(audio, (UsbSetup){0x81, 10, 0, 4, 1}).kind);
        break;
    case 5:
        CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(audio, (UsbSetup){0x81, 10, 1, 1, 1}).kind);
        break;
    }
}

static void check_state(UsbAudio *audio, const UsbModel *model)
{
    UsbSetup rate = rate_setup(model);
    rate.request_type |= 0x80;
    rate.request = model->high ? 1 : 0x81;
    uint8_t request = rate.request;
    CHECK_EQUAL(model->rate, response_value(usb_audio_setup(audio, rate), rate.length));
    CHECK_EQUAL(model->alt,
                response_value(usb_audio_setup(audio, (UsbSetup){0x81, 10, 0, 1, 1}), 1));
    CHECK_EQUAL(model->alt ? model->alt + 1 : 0, usb_audio_sample_bytes(audio));
    CHECK_EQUAL(
        (uint16_t)model->volume,
        response_value(usb_audio_setup(audio, (UsbSetup){0xA1, request, 0x0200, 0x0200, 2}), 2));
    CHECK_EQUAL(
        model->muted,
        response_value(usb_audio_setup(audio, (UsbSetup){0xA1, request, 0x0100, 0x0200, 1}), 1));
    CHECK_EQUAL(model->changes, audio->changes);
}

static void run_sequence(bool high)
{
    UsbDevice device;
    UsbAudio audio;
    usb_audio_init(&audio, &device);
    device.speed = high ? USB_SPEED_HIGH : USB_SPEED_FULL;
    UsbModel model = {.rate = 48000, .high = high};
    uint32_t seed = high ? 0xA3B129CFu : 0x7BE66127u;
    for (uint32_t step = 0; step < 1024; step++) {
        uint32_t random = next_random(&seed);
        uint32_t choice = random >> 8;
        switch (random % 8) {
        case 0:
            select_alt(&audio, &model, choice % 4);
            break;
        case 1:
            write_rate(&audio, &model, choice % TEST_CASE_COUNT(RATES));
            break;
        case 2:
            write_volume(&audio, &model, choice);
            break;
        case 3:
            write_mute(&audio, &model, (choice & 1u) != 0);
            break;
        case 4:
            read_range(&audio, &model);
            break;
        case 5:
            reject_malformed(&audio, &model, choice);
            break;
        case 6:
            usb_audio_reset(&audio);
            model.alt = 0;
            model.rate = 48000;
            model.changes |= USB_AUDIO_CHANGED_FORMAT;
            break;
        case 7:
            CHECK_EQUAL(model.changes, usb_audio_take_changes(&audio));
            model.changes = 0;
            CHECK_EQUAL(0, usb_audio_take_changes(&audio));
            break;
        }
        check_state(&audio, &model);
    }
}

static void reset_publishes_only_the_current_stream_format(void)
{
    UsbDevice device;
    UsbAudio audio;
    usb_audio_init(&audio, &device);
    device.speed = USB_SPEED_HIGH;
    UsbModel model = {.rate = 48000, .high = true};
    select_alt(&audio, &model, 3);
    write_rate(&audio, &model, 5);
    usb_audio_reset(&audio);
    CHECK_EQUAL(0, usb_audio_sample_bytes(&audio));
    CHECK_EQUAL(48000, audio.rate);
    CHECK_EQUAL(USB_AUDIO_CHANGED_FORMAT, usb_audio_take_changes(&audio));
    CHECK_EQUAL(0, usb_audio_take_changes(&audio));
}

static void full_speed_control_transitions(void)
{
    run_sequence(false);
}

static void high_speed_control_transitions(void)
{
    run_sequence(true);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(full_speed_control_transitions),
        TEST_CASE(high_speed_control_transitions),
        TEST_CASE(reset_publishes_only_the_current_stream_format),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
