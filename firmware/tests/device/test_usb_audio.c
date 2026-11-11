#include <stddef.h>

#include "check.h"
#include "device/usb_audio.h"

enum {
    CONFIGURATION_LENGTH = 184,
    FORMAT_LENGTH = 14,
    RATE_COUNT = 2
};

static const uint32_t RATES[RATE_COUNT] = {44100, 48000};

static UsbDevice device;

static UsbSetup setup(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                      uint16_t length)
{
    return (UsbSetup){type, request, value, index, length};
}

static UsbAudio ready_audio(void)
{
    UsbAudio audio;
    usb_audio_init(&audio, &device);
    return audio;
}

static UsbReply configuration(UsbAudio *audio)
{
    return usb_audio_setup(audio, setup(0x80, 6, 0x0200, 0, 255));
}

static const uint8_t *find_descriptor(UsbReply reply, uint8_t type, int occurrence)
{
    for (uint16_t offset = 0; offset < reply.length; offset += reply.data[offset]) {
        if (reply.data[offset + 1] == type && occurrence-- == 0) {
            return reply.data + offset;
        }
    }
    return NULL;
}

static uint32_t rate_at(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16;
}

static void set_cur(UsbAudio *audio, UsbSetup request, const uint8_t *bytes, uint16_t length)
{
    CHECK_EQUAL(USB_REPLY_RECEIVE, usb_audio_setup(audio, request).kind);
    CHECK_EQUAL(USB_REPLY_STATUS, usb_audio_receive(audio, request, bytes, length).kind);
}

static void the_device_is_a_full_speed_composite_of_audio_interfaces(void)
{
    UsbAudio audio = ready_audio();
    UsbReply reply = usb_audio_setup(&audio, setup(0x80, 6, 0x0100, 0, 64));
    CHECK_EQUAL(18, reply.length);
    CHECK_EQUAL(0x00, reply.data[2]);
    CHECK_EQUAL(0x02, reply.data[3]);
    CHECK_EQUAL(0, reply.data[4]);
    CHECK_EQUAL(0x02, reply.data[10]);
    CHECK_EQUAL(0, reply.data[16]);
}

static void the_configuration_walks_to_its_declared_length(void)
{
    UsbAudio audio = ready_audio();
    UsbReply reply = configuration(&audio);
    CHECK_EQUAL(CONFIGURATION_LENGTH, reply.length);
    CHECK_EQUAL(CONFIGURATION_LENGTH, reply.data[2]);
    uint16_t offset = 0;
    while (offset < reply.length && reply.data[offset] > 0) {
        offset += reply.data[offset];
    }
    CHECK_EQUAL(CONFIGURATION_LENGTH, offset);
}

static void the_control_header_covers_its_units(void)
{
    UsbAudio audio = ready_audio();
    UsbReply reply = configuration(&audio);
    const uint8_t *header = find_descriptor(reply, 0x24, 0);
    CHECK_EQUAL(1, header[2]);
    uint16_t total = 0;
    for (const uint8_t *unit = header; unit[1] == 0x24; unit += unit[0]) {
        total += unit[0];
    }
    CHECK_EQUAL(header[5], total);
}

static const uint8_t *format_of_setting(UsbReply reply, int setting)
{
    for (uint16_t offset = 0; offset < reply.length; offset += reply.data[offset]) {
        const uint8_t *bytes = reply.data + offset;
        if (bytes[0] == FORMAT_LENGTH && bytes[1] == 0x24 && bytes[2] == 2 && setting-- == 0) {
            return bytes;
        }
    }
    return NULL;
}

static void both_streaming_settings_offer_the_same_two_rates(void)
{
    UsbAudio audio = ready_audio();
    UsbReply reply = configuration(&audio);
    for (int setting = 0; setting < 2; setting++) {
        const uint8_t *format = format_of_setting(reply, setting);
        CHECK(format != NULL);
        CHECK_EQUAL(setting == 0 ? 2 : 3, format[5]);
        CHECK_EQUAL(setting == 0 ? 16 : 24, format[6]);
        CHECK_EQUAL(RATE_COUNT, format[7]);
        for (int index = 0; index < RATE_COUNT; index++) {
            CHECK_EQUAL(RATES[index], rate_at(format + 8 + 3 * index));
        }
    }
}

static void each_streaming_endpoint_fits_forty_nine_frames(void)
{
    UsbAudio audio = ready_audio();
    UsbReply reply = configuration(&audio);
    const uint8_t *sixteen = find_descriptor(reply, 5, 0);
    const uint8_t *twenty_four = find_descriptor(reply, 5, 1);
    CHECK_EQUAL(USB_AUDIO_ENDPOINT, sixteen[2]);
    CHECK_EQUAL(0x09, sixteen[3]);
    CHECK_EQUAL(49 * 4, sixteen[4] | sixteen[5] << 8);
    CHECK_EQUAL(USB_AUDIO_MAX_PACKET, twenty_four[4] | twenty_four[5] << 8);
    CHECK_EQUAL(49 * 6, USB_AUDIO_MAX_PACKET);
}

static void every_offered_format_fits_one_armed_packet(void)
{
    for (uint32_t sample_bytes = 2; sample_bytes <= 3; sample_bytes++) {
        for (int index = 0; index < RATE_COUNT; index++) {
            uint32_t frames = (RATES[index] + 999) / 1000 + 1;
            CHECK(frames * 2 * sample_bytes <= USB_AUDIO_MAX_PACKET);
        }
    }
}

static void choosing_a_streaming_setting_reports_the_sample_size(void)
{
    UsbAudio audio = ready_audio();
    UsbReply reply = usb_audio_setup(&audio, setup(0x01, 11, 2, 1, 0));
    CHECK_EQUAL(USB_ACTION_STREAM, reply.action);
    CHECK_EQUAL(2, reply.value);
    CHECK_EQUAL(3, usb_audio_sample_bytes(&audio));
    CHECK_EQUAL(USB_AUDIO_CHANGED_FORMAT, usb_audio_take_changes(&audio));
    CHECK_EQUAL(0, usb_audio_take_changes(&audio));
    usb_audio_setup(&audio, setup(0x01, 11, 1, 1, 0));
    CHECK_EQUAL(2, usb_audio_sample_bytes(&audio));
    reply = usb_audio_setup(&audio, setup(0x81, 10, 0, 1, 1));
    CHECK_EQUAL(1, reply.data[0]);
}

static void unknown_settings_stall(void)
{
    UsbAudio audio = ready_audio();
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(&audio, setup(0x01, 11, 3, 1, 0)).kind);
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(&audio, setup(0x01, 11, 1, 0, 0)).kind);
    CHECK_EQUAL(USB_REPLY_STATUS, usb_audio_setup(&audio, setup(0x01, 11, 0, 0, 0)).kind);
}

static void a_bus_reset_or_new_configuration_stops_streaming(void)
{
    UsbAudio audio = ready_audio();
    usb_audio_setup(&audio, setup(0x01, 11, 1, 1, 0));
    usb_audio_reset(&audio);
    CHECK_EQUAL(0, usb_audio_sample_bytes(&audio));
    usb_audio_setup(&audio, setup(0x01, 11, 1, 1, 0));
    UsbReply reply = usb_audio_setup(&audio, setup(0x00, 9, 1, 0, 0));
    CHECK_EQUAL(USB_ACTION_CONFIGURE, reply.action);
    CHECK_EQUAL(0, usb_audio_sample_bytes(&audio));
}

static void the_host_sets_and_reads_the_sample_rate(void)
{
    UsbAudio audio = ready_audio();
    CHECK_EQUAL(48000, audio.rate);
    static const uint8_t rate[] = {0x44, 0xAC, 0x00};
    UsbSetup request = setup(0x22, 0x01, 0x0100, 0x01, 3);
    set_cur(&audio, request, rate, 3);
    CHECK_EQUAL(44100, audio.rate);
    CHECK_EQUAL(USB_AUDIO_CHANGED_FORMAT, usb_audio_take_changes(&audio));
    UsbReply reply = usb_audio_setup(&audio, setup(0xA2, 0x81, 0x0100, 0x01, 3));
    CHECK_EQUAL(3, reply.length);
    CHECK_EQUAL(44100, rate_at(reply.data));
}

static void an_unsupported_rate_stalls_and_keeps_the_old_one(void)
{
    UsbAudio audio = ready_audio();
    static const uint8_t rates[][3] = {{0x00, 0xEE, 0x02}, {0x00, 0x77, 0x01}, {0x88, 0x58, 0x01}};
    for (size_t index = 0; index < sizeof rates / sizeof rates[0]; index++) {
        UsbReply reply =
            usb_audio_receive(&audio, setup(0x22, 0x01, 0x0100, 0x01, 3), rates[index], 3);
        CHECK_EQUAL(USB_REPLY_STALL, reply.kind);
        CHECK_EQUAL(48000, audio.rate);
    }
}

static void the_volume_range_is_minus_sixty_four_to_zero_decibels(void)
{
    UsbAudio audio = ready_audio();
    UsbReply minimum = usb_audio_setup(&audio, setup(0xA1, 0x82, 0x0200, 0x0200, 2));
    CHECK_EQUAL(0x00, minimum.data[0]);
    CHECK_EQUAL(0xC0, minimum.data[1]);
    UsbReply resolution = usb_audio_setup(&audio, setup(0xA1, 0x84, 0x0200, 0x0200, 2));
    CHECK_EQUAL(0x80, resolution.data[0]);
    UsbReply maximum = usb_audio_setup(&audio, setup(0xA1, 0x83, 0x0200, 0x0200, 2));
    CHECK_EQUAL(0, maximum.data[0] | maximum.data[1]);
}

static void host_volume_becomes_half_decibel_attenuation(void)
{
    UsbAudio audio = ready_audio();
    CHECK_EQUAL(0, usb_audio_attenuation(&audio));
    static const uint8_t minus_ten_db[] = {0x00, 0xF6};
    set_cur(&audio, setup(0x21, 0x01, 0x0200, 0x0200, 2), minus_ten_db, 2);
    CHECK_EQUAL(20, usb_audio_attenuation(&audio));
    CHECK_EQUAL(USB_AUDIO_CHANGED_VOLUME, usb_audio_take_changes(&audio));
    UsbReply reply = usb_audio_setup(&audio, setup(0xA1, 0x81, 0x0200, 0x0200, 2));
    CHECK_EQUAL(0xF6, reply.data[1]);
}

static void volume_below_the_range_is_clamped(void)
{
    UsbAudio audio = ready_audio();
    static const uint8_t silent[] = {0x00, 0x80};
    set_cur(&audio, setup(0x21, 0x01, 0x0200, 0x0200, 2), silent, 2);
    CHECK_EQUAL(128, usb_audio_attenuation(&audio));
}

static void mute_overrides_the_volume(void)
{
    UsbAudio audio = ready_audio();
    static const uint8_t on[] = {1};
    set_cur(&audio, setup(0x21, 0x01, 0x0100, 0x0200, 1), on, 1);
    CHECK_EQUAL(USB_AUDIO_MUTED, usb_audio_attenuation(&audio));
    UsbReply reply = usb_audio_setup(&audio, setup(0xA1, 0x81, 0x0100, 0x0200, 1));
    CHECK_EQUAL(1, reply.data[0]);
}

static void requests_for_other_units_or_channels_stall(void)
{
    UsbAudio audio = ready_audio();
    CHECK_EQUAL(USB_REPLY_STALL,
                usb_audio_setup(&audio, setup(0xA1, 0x81, 0x0201, 0x0200, 2)).kind);
    CHECK_EQUAL(USB_REPLY_STALL,
                usb_audio_setup(&audio, setup(0x21, 0x01, 0x0200, 0x0500, 2)).kind);
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(&audio, setup(0xA1, 0xFE, 0, 0, 1)).kind);
}

static void standard_requests_reach_the_usb_device(void)
{
    UsbAudio audio = ready_audio();
    UsbReply address = usb_audio_setup(&audio, setup(0x00, 5, 7, 0, 0));
    CHECK_EQUAL(USB_ACTION_SET_ADDRESS, address.action);
    CHECK_EQUAL(7, address.value);
    CHECK_EQUAL(USB_REPLY_DATA, usb_audio_setup(&audio, setup(0x80, 6, 0x0600, 0, 10)).kind);
    CHECK_EQUAL(USB_REPLY_DATA, usb_audio_setup(&audio, setup(0x80, 6, 0x0302, 0, 64)).kind);
}

static void high_speed_offers_three_asynchronous_formats_and_a_clock_source(void)
{
    UsbAudio audio = ready_audio();
    usb_device_set_speed(&device, USB_SPEED_HIGH);
    UsbReply reply = usb_audio_setup(&audio, setup(0x80, 6, 0x0200, 0, 1024));
    CHECK_EQUAL(3, reply.data[4]);
    CHECK_EQUAL(reply.length, reply.data[2] | reply.data[3] << 8);
    int formats = 0;
    int feedbacks = 0;
    int clocks = 0;
    for (uint16_t offset = 0; offset < reply.length;) {
        const uint8_t *part = reply.data + offset;
        CHECK(part[0] >= 2 && offset + part[0] <= reply.length);
        if (part[1] == 0x24 && part[2] == 0x0A) {
            clocks++;
            CHECK_EQUAL(4, part[3]);
            CHECK_EQUAL(7, part[5]);
        }
        if (part[1] == 0x24 && part[2] == 2 && part[0] == 6) {
            CHECK_EQUAL(formats + 2, part[4]);
            CHECK_EQUAL((formats + 2) * 8, part[5]);
            formats++;
        }
        if (part[1] == 5 && part[2] == 0x01) {
            CHECK_EQUAL(0x05, part[3]);
            CHECK_EQUAL(1, part[6]);
            CHECK_EQUAL(25 * 2 * (formats + 1), part[4] | part[5] << 8);
        }
        if (part[1] == 5 && part[3] == 0x11) {
            feedbacks++;
            CHECK_EQUAL(4, part[4]);
        }
        offset += part[0];
    }
    CHECK_EQUAL(3, formats);
    CHECK_EQUAL(3, feedbacks);
    CHECK_EQUAL(1, clocks);
}

static void the_high_speed_clock_lists_and_accepts_all_six_rates(void)
{
    static const uint32_t rates[] = {44100, 48000, 88200, 96000, 176400, 192000};
    UsbAudio audio = ready_audio();
    usb_device_set_speed(&device, USB_SPEED_HIGH);
    UsbReply reply = usb_audio_setup(&audio, setup(0xA1, 2, 0x0100, 0x0400, 255));
    CHECK_EQUAL(USB_REPLY_DATA, reply.kind);
    CHECK_EQUAL(74, reply.length);
    if (reply.kind != USB_REPLY_DATA || reply.length != 74) {
        return;
    }
    CHECK_EQUAL(6, reply.data[0]);
    for (int index = 0; index < 6; index++) {
        CHECK_EQUAL(rates[index], rate_at(reply.data + 2 + index * 12));
        CHECK_EQUAL(rates[index], rate_at(reply.data + 6 + index * 12));
        CHECK_EQUAL(0, rate_at(reply.data + 10 + index * 12));
    }
    for (int index = 0; index < 6; index++) {
        uint8_t bytes[] = {(uint8_t)rates[index], (uint8_t)(rates[index] >> 8),
                           (uint8_t)(rates[index] >> 16), 0};
        set_cur(&audio, setup(0x21, 1, 0x0100, 0x0400, 4), bytes, 4);
        CHECK_EQUAL(rates[index], audio.rate);
    }
    CHECK_EQUAL(USB_REPLY_STATUS, usb_audio_setup(&audio, setup(0x01, 11, 3, 1, 0)).kind);
    CHECK_EQUAL(4, usb_audio_sample_bytes(&audio));
}

static void other_speed_descriptors_and_clock_controls_do_not_cross_protocols(void)
{
    UsbAudio audio = ready_audio();
    UsbReply reply = usb_audio_setup(&audio, setup(0x80, 6, 0x0700, 0, 1024));
    CHECK_EQUAL(7, reply.data[1]);
    CHECK_EQUAL(283, reply.length);
    usb_device_set_speed(&device, USB_SPEED_HIGH);
    reply = usb_audio_setup(&audio, setup(0x80, 6, 0x0700, 0, 1024));
    CHECK_EQUAL(184, reply.length);
    CHECK_EQUAL(7, reply.data[1]);
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(&audio, setup(0xA2, 0x81, 0x0100, 1, 3)).kind);
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(&audio, setup(0x21, 1, 0x0101, 0x0400, 4)).kind);
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(&audio, setup(0x21, 1, 0x0100, 0x0400, 3)).kind);
    const uint8_t rate[] = {0x80, 0xBB, 0, 0};
    CHECK_EQUAL(USB_REPLY_STALL,
                usb_audio_receive(&audio, setup(0x21, 2, 0x0100, 0x0400, 4), rate, 4).kind);
    CHECK_EQUAL(USB_REPLY_STALL,
                usb_audio_receive(&audio, setup(0x21, 1, 0x0100, 0x0400, 4), rate, 3).kind);
    reply = usb_audio_setup(&audio, setup(0xA1, 2, 0x0100, 0x0400, 2));
    CHECK_EQUAL(2, reply.length);
    CHECK_EQUAL(6, reply.data[0]);
    reply = usb_audio_setup(&audio, setup(0xA1, 1, 0x0200, 0x0400, 1));
    CHECK_EQUAL(1, reply.data[0]);
    reply = usb_audio_setup(&audio, setup(0xA1, 2, 0x0200, 0x0200, 8));
    CHECK_EQUAL(8, reply.length);
    CHECK_EQUAL(1, reply.data[0]);
    CHECK_EQUAL(0xC0, reply.data[3]);
    CHECK_EQUAL(0x80, reply.data[6]);
}

static void hid_enumeration_and_idle_requests_use_the_consumer_interface(void)
{
    UsbAudio audio = ready_audio();
    UsbReply reply = usb_audio_setup(&audio, setup(0x81, 6, 0x2200, 2, 255));
    CHECK_EQUAL(USB_REPLY_DATA, reply.kind);
    CHECK_EQUAL(23, reply.length);
    CHECK_EQUAL(0x0C, reply.data[1]);
    CHECK_EQUAL(USB_REPLY_STATUS, usb_audio_setup(&audio, setup(0x21, 10, 0x0500, 2, 0)).kind);
    reply = usb_audio_setup(&audio, setup(0xA1, 2, 0, 2, 1));
    CHECK_EQUAL(5, reply.data[0]);
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(&audio, setup(0x21, 10, 1, 2, 0)).kind);
    usb_audio_reset(&audio);
    CHECK_EQUAL(0, audio.hid_idle);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(other_speed_descriptors_and_clock_controls_do_not_cross_protocols),
        TEST_CASE(hid_enumeration_and_idle_requests_use_the_consumer_interface),
        TEST_CASE(high_speed_offers_three_asynchronous_formats_and_a_clock_source),
        TEST_CASE(the_high_speed_clock_lists_and_accepts_all_six_rates),
        TEST_CASE(the_device_is_a_full_speed_composite_of_audio_interfaces),
        TEST_CASE(the_configuration_walks_to_its_declared_length),
        TEST_CASE(the_control_header_covers_its_units),
        TEST_CASE(both_streaming_settings_offer_the_same_two_rates),
        TEST_CASE(each_streaming_endpoint_fits_forty_nine_frames),
        TEST_CASE(every_offered_format_fits_one_armed_packet),
        TEST_CASE(choosing_a_streaming_setting_reports_the_sample_size),
        TEST_CASE(unknown_settings_stall),
        TEST_CASE(a_bus_reset_or_new_configuration_stops_streaming),
        TEST_CASE(the_host_sets_and_reads_the_sample_rate),
        TEST_CASE(an_unsupported_rate_stalls_and_keeps_the_old_one),
        TEST_CASE(the_volume_range_is_minus_sixty_four_to_zero_decibels),
        TEST_CASE(host_volume_becomes_half_decibel_attenuation),
        TEST_CASE(volume_below_the_range_is_clamped),
        TEST_CASE(mute_overrides_the_volume),
        TEST_CASE(requests_for_other_units_or_channels_stall),
        TEST_CASE(standard_requests_reach_the_usb_device),

    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
