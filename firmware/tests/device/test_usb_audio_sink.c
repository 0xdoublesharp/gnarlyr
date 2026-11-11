#include "check.h"
#include "device/usb_audio_sink.h"

enum {
    RING_FRAMES = 4000,
    RATE = 48000,
    TARGET = RATE / 50,
    PACKET_FRAMES = 48
};

static uint32_t ring[RING_FRAMES * 2];

static UsbAudioSink started_sink(uint8_t sample_bytes)
{
    UsbAudioSink sink;
    for (uint32_t index = 0; index < RING_FRAMES * 2; index++) {
        ring[index] = 0xDEADBEEFu;
    }
    usb_audio_sink_init(&sink, ring, RING_FRAMES);
    usb_audio_sink_start(&sink, RATE, sample_bytes);
    return sink;
}

static void packet_16(uint8_t *bytes, uint32_t frames, uint8_t first)
{
    for (uint32_t frame = 0; frame < frames; frame++) {
        for (uint32_t byte = 0; byte < 4; byte++) {
            bytes[frame * 4 + byte] = (uint8_t)(first + frame);
        }
    }
}

static void write_packets(UsbAudioSink *sink, uint32_t count, uint32_t consumed)
{
    uint8_t bytes[PACKET_FRAMES * 4];
    packet_16(bytes, PACKET_FRAMES, 1);
    for (uint32_t index = 0; index < count; index++) {
        usb_audio_sink_write(sink, bytes, sizeof bytes, consumed);
    }
}

static void sixteen_bit_samples_go_to_the_top_of_each_word(void)
{
    UsbAudioSink sink = started_sink(2);
    static const uint8_t bytes[] = {0x34, 0x12, 0xCD, 0xAB};
    usb_audio_sink_write(&sink, bytes, sizeof bytes, 0);
    CHECK_EQUAL(0x12340000u, ring[0]);
    CHECK_EQUAL(0xABCD0000u, ring[1]);
    CHECK_EQUAL(1, sink.written);
}

static void twenty_four_bit_samples_are_left_justified(void)
{
    UsbAudioSink sink = started_sink(3);
    static const uint8_t bytes[] = {0x56, 0x34, 0x12, 0xEF, 0xCD, 0xAB};
    usb_audio_sink_write(&sink, bytes, sizeof bytes, 0);
    CHECK_EQUAL(0x12345600u, ring[0]);
    CHECK_EQUAL(0xABCDEF00u, ring[1]);
}

static void thirty_two_bit_samples_preserve_every_bit_at_unity_gain(void)
{
    UsbAudioSink sink = started_sink(4);
    static const uint8_t bytes[] = {0x67, 0x45, 0x23, 0x01, 0xFF, 0xFF, 0xFF, 0x7F};
    usb_audio_sink_write(&sink, bytes, sizeof bytes, 0);
    CHECK_EQUAL(0x01234567u, ring[0]);
    CHECK_EQUAL(0x7FFFFFFFu, ring[1]);
}

static void a_partial_frame_at_the_end_of_a_packet_is_ignored(void)
{
    UsbAudioSink sink = started_sink(3);
    static const uint8_t bytes[8] = {0};
    usb_audio_sink_write(&sink, bytes, sizeof bytes, 0);
    CHECK_EQUAL(1, sink.written);
}

static void nothing_is_written_while_streaming_is_off(void)
{
    UsbAudioSink sink = started_sink(0);
    static const uint8_t bytes[8] = {0};
    usb_audio_sink_write(&sink, bytes, sizeof bytes, 0);
    CHECK_EQUAL(0, sink.written);
    CHECK(!usb_audio_sink_primed(&sink));
}

static void the_sink_is_primed_after_twenty_milliseconds(void)
{
    UsbAudioSink sink = started_sink(2);
    write_packets(&sink, TARGET / PACKET_FRAMES - 1, 0);
    CHECK(!usb_audio_sink_primed(&sink));
    write_packets(&sink, 1, 0);
    CHECK(usb_audio_sink_primed(&sink));
}

static void silence_is_kept_ahead_of_the_last_packet(void)
{
    UsbAudioSink sink = started_sink(2);
    write_packets(&sink, 1, 0);
    CHECK_EQUAL(0u, ring[2 * PACKET_FRAMES]);
    CHECK_EQUAL(0u, ring[2 * (PACKET_FRAMES + TARGET) - 1]);
    CHECK_EQUAL(0xDEADBEEFu, ring[2 * (PACKET_FRAMES + TARGET)]);
}

static void a_full_buffer_while_playing_drops_one_frame_per_packet(void)
{
    UsbAudioSink sink = started_sink(2);
    write_packets(&sink, 40, 0);
    usb_audio_sink_set_playing(&sink, true);
    uint32_t before = sink.written;
    write_packets(&sink, 1, before - TARGET - 400);
    CHECK_EQUAL(before + PACKET_FRAMES - 1, sink.written);
    CHECK_EQUAL(1, sink.stats.dropped);
}

static void a_low_buffer_while_playing_repeats_the_last_frame(void)
{
    UsbAudioSink sink = started_sink(2);
    write_packets(&sink, 10, 0);
    usb_audio_sink_set_playing(&sink, true);
    uint32_t before = sink.written;
    uint8_t bytes[PACKET_FRAMES * 4];
    packet_16(bytes, PACKET_FRAMES, 1);
    usb_audio_sink_write(&sink, bytes, sizeof bytes, before - TARGET + 400);
    CHECK_EQUAL(before + PACKET_FRAMES + 1, sink.written);
    CHECK_EQUAL(ring[2 * (before + PACKET_FRAMES - 1)], ring[2 * (before + PACKET_FRAMES)]);
    CHECK_EQUAL(1, sink.stats.repeated);
}

static void no_frames_are_dropped_or_repeated_before_playing(void)
{
    UsbAudioSink sink = started_sink(2);
    write_packets(&sink, 3, 0);
    CHECK_EQUAL(3 * PACKET_FRAMES, sink.written);
}

static void an_underrun_restarts_twenty_milliseconds_ahead_of_the_output(void)
{
    UsbAudioSink sink = started_sink(2);
    write_packets(&sink, 30, 0);
    usb_audio_sink_set_playing(&sink, true);
    uint32_t consumed = sink.written + 100;
    write_packets(&sink, 1, consumed);
    CHECK_EQUAL(1, sink.stats.realigned);
    CHECK_EQUAL(consumed + TARGET + PACKET_FRAMES, sink.written);
    CHECK_EQUAL(0u, ring[2 * consumed]);
}

static void an_overflow_also_restarts_near_the_output(void)
{
    UsbAudioSink sink = started_sink(2);
    write_packets(&sink, RING_FRAMES / PACKET_FRAMES, 0);
    CHECK(sink.stats.realigned > 0);
    CHECK(sink.written <= RING_FRAMES);
}

static void writing_wraps_around_the_end_of_the_ring(void)
{
    UsbAudioSink sink = started_sink(2);
    usb_audio_sink_set_playing(&sink, true);
    uint32_t consumed = 0;
    for (int packet = 0; packet < 200; packet++) {
        write_packets(&sink, 1, consumed);
        consumed += PACKET_FRAMES;
    }
    CHECK_EQUAL(0, sink.stats.realigned);
    CHECK(sink.written > RING_FRAMES);
    CHECK_EQUAL(0x30300000u, ring[2 * ((sink.written - 1) % RING_FRAMES)]);
}

static void put_16(uint8_t *bytes, int16_t left, int16_t right)
{
    bytes[0] = (uint8_t)left;
    bytes[1] = (uint8_t)((uint16_t)left >> 8);
    bytes[2] = (uint8_t)right;
    bytes[3] = (uint8_t)((uint16_t)right >> 8);
}

static void peaks_follow_the_loudest_sample_of_each_channel_until_taken(void)
{
    UsbAudioSink sink = started_sink(2);
    uint8_t bytes[3 * 4];
    put_16(bytes, 0x0100, -0x0200);
    put_16(bytes + 4, -0x1000, 0x0300);
    put_16(bytes + 8, 0x0400, -0x2000);
    usb_audio_sink_write(&sink, bytes, sizeof bytes, 0);
    uint16_t peaks[2];
    usb_audio_sink_take_peaks(&sink, peaks);
    CHECK_EQUAL(0x1000, peaks[0]);
    CHECK_EQUAL(0x2000, peaks[1]);
    usb_audio_sink_take_peaks(&sink, peaks);
    CHECK_EQUAL(0, peaks[0]);
    CHECK_EQUAL(0, peaks[1]);
}

static void the_most_negative_sample_is_a_full_scale_peak(void)
{
    UsbAudioSink sink = started_sink(2);
    uint8_t bytes[4];
    put_16(bytes, -32768, 32767);
    usb_audio_sink_write(&sink, bytes, sizeof bytes, 0);
    uint16_t peaks[2];
    usb_audio_sink_take_peaks(&sink, peaks);
    CHECK_EQUAL(32768, peaks[0]);
    CHECK_EQUAL(32767, peaks[1]);
}

static void headphones_trim_both_usb_formats_and_survive_rate_restarts(void)
{
    for (uint8_t width = 2; width <= 3; width++) {
        UsbAudioSink sink = started_sink(width);
        HeadphoneAudio profile = {
            .left = REPLAY_GAIN_UNITY / 4, .right = REPLAY_GAIN_UNITY / 2, .ceiling = 25};
        usb_audio_sink_set_headphones(&sink, profile);
        uint8_t bytes[] = {0xFF, 0x7F, 0x00, 0x80, 0, 0};
        if (width == 3) {
            const uint8_t wide[] = {0xFF, 0xFF, 0x7F, 0, 0, 0x80};
            for (int index = 0; index < 6; index++) {
                bytes[index] = wide[index];
            }
        }
        for (int rate = 44100; rate <= 48000; rate += 3900) {
            usb_audio_sink_start(&sink, (uint32_t)rate, width);
            usb_audio_sink_write(&sink, bytes, 2u * width, 0);
            CHECK_EQUAL(headphone_audio_word(width == 2 ? 0x7FFF0000u : 0x7FFFFF00u, profile.left),
                        ring[0]);
            CHECK_EQUAL(0xC0000000u, ring[1]);
        }
        usb_audio_sink_set_headphones(&sink, headphone_audio_defaults());
        uint32_t next = sink.written;
        usb_audio_sink_write(&sink, bytes, 2u * width, 0);
        CHECK_EQUAL(width == 2 ? 0x7FFF0000u : 0x7FFFFF00u, ring[2 * next]);
    }
}

static void asynchronous_audio_never_drops_or_repeats_frames_to_correct_fill(void)
{
    UsbAudioSink sink = started_sink(4);
    usb_audio_sink_set_asynchronous(&sink, true);
    usb_audio_sink_start(&sink, 192000, 4);
    CHECK_EQUAL(960, sink.target);
    CHECK_EQUAL(192, sink.guard);
    uint8_t bytes[192] = {0};
    for (int packet = 0; packet < 80; packet++) {
        usb_audio_sink_write(&sink, bytes, sizeof bytes, 0);
    }
    usb_audio_sink_set_playing(&sink, true);
    uint32_t before = sink.written;
    usb_audio_sink_write(&sink, bytes, sizeof bytes, before - sink.target - 800);
    CHECK_EQUAL(before + 24, sink.written);
    usb_audio_sink_write(&sink, bytes, sizeof bytes, sink.written - 100);
    CHECK_EQUAL(0, sink.stats.dropped);
    CHECK_EQUAL(0, sink.stats.repeated);
    CHECK_EQUAL(0, sink.stats.realigned);
}

static void feedback_tracks_the_output_clock_and_steers_the_host_toward_target_fill(void)
{
    UsbAudioSink sink = started_sink(4);
    usb_audio_sink_set_asynchronous(&sink, true);
    usb_audio_sink_start(&sink, 192000, 4);
    CHECK_EQUAL(24u << 16, usb_audio_sink_feedback(&sink, 0, 0));
    usb_audio_sink_set_playing(&sink, true);
    sink.written = sink.target;
    uint32_t nominal = usb_audio_sink_feedback(&sink, 0, 16000);
    sink.written = sink.target + 24576;
    CHECK_EQUAL(nominal, usb_audio_sink_feedback(&sink, 24576, (16000 + 1024) & 0x3FFF));
    sink.written += 400;
    CHECK(usb_audio_sink_feedback(&sink, 24576, 649) < nominal);
    sink.written -= 800;
    CHECK(usb_audio_sink_feedback(&sink, 24576, 650) > nominal);
    sink.written = sink.target + 49155;
    CHECK(usb_audio_sink_feedback(&sink, 49155, (16000 + 2048) & 0x3FFF) > nominal);
}

static void stream_with_clock_error(uint32_t rate, uint8_t width, int32_t ppm)
{
    UsbAudioSink sink;
    usb_audio_sink_init(&sink, ring, 2200);
    usb_audio_sink_set_asynchronous(&sink, true);
    usb_audio_sink_start(&sink, rate, width);
    uint64_t host_phase = 0;
    uint32_t played_tick = 0;
    uint32_t consumed = 0;
    uint8_t packet[200];
    for (uint32_t tick = 0; tick < 80000; tick++) {
        if (sink.playing) {
            consumed = (uint32_t)((uint64_t)(tick - played_tick) * rate *
                                  (uint32_t)(1000000 + ppm) / UINT64_C(8000000000));
        }
        host_phase += usb_audio_sink_feedback(&sink, consumed, (uint16_t)(tick & 0x3FFF));
        uint32_t frames = (uint32_t)(host_phase >> 16);
        host_phase &= 0xFFFF;
        CHECK(frames * width * 2 <= sizeof packet);
        for (uint32_t frame = 0; frame < frames; frame++) {
            for (uint8_t channel = 0; channel < 2; channel++) {
                uint32_t word = (sink.written + frame + channel) << (8 * (4 - width));
                for (uint8_t byte = 0; byte < width; byte++) {
                    packet[(frame * 2 + channel) * width + byte] =
                        (uint8_t)(word >> (8 * (4 - width + byte)));
                }
            }
        }
        usb_audio_sink_write(&sink, packet, frames * width * 2, consumed);
        if (!sink.playing && usb_audio_sink_primed(&sink)) {
            usb_audio_sink_set_playing(&sink, true);
            played_tick = tick;
        }
        if (sink.playing) {
            int32_t fill = (int32_t)(sink.written - consumed);
            CHECK(fill > (int32_t)sink.target / 2);
            CHECK(fill < (int32_t)sink.target * 2);
        }
    }
    CHECK_EQUAL(0, sink.stats.realigned);
    CHECK_EQUAL(0, sink.stats.underruns);
    CHECK_EQUAL(0, sink.stats.overflows);
    CHECK_EQUAL(0, sink.stats.dropped);
    CHECK_EQUAL(0, sink.stats.repeated);
    for (uint32_t frame = sink.written - 100; frame < sink.written; frame++) {
        for (uint8_t channel = 0; channel < 2; channel++) {
            CHECK_EQUAL((frame + channel) << (8 * (4 - width)), ring[(frame % 2200) * 2 + channel]);
        }
    }
}

static void every_rate_and_width_streams_bit_exact_with_fast_and_slow_device_clocks(void)
{
    static const uint32_t rates[] = {44100, 48000, 88200, 96000, 176400, 192000};
    for (unsigned index = 0; index < sizeof rates / sizeof rates[0]; index++) {
        for (uint8_t width = 2; width <= 4; width++) {
            stream_with_clock_error(rates[index], width, -300);
            stream_with_clock_error(rates[index], width, 300);
        }
    }
}

static void a_two_millisecond_delivery_burst_stays_inside_the_192_khz_buffer(void)
{
    UsbAudioSink sink;
    usb_audio_sink_init(&sink, ring, 2200);
    usb_audio_sink_set_asynchronous(&sink, true);
    usb_audio_sink_start(&sink, 192000, 4);
    uint8_t packet[192] = {0};
    for (int index = 0; index < 40; index++) {
        usb_audio_sink_write(&sink, packet, sizeof packet, 0);
    }
    usb_audio_sink_set_playing(&sink, true);
    uint32_t consumed = 0;
    for (int burst = 0; burst < 1000; burst++) {
        consumed += 384;
        for (int index = 0; index < 16; index++) {
            usb_audio_sink_write(&sink, packet, sizeof packet, consumed);
        }
        CHECK_EQUAL(960, sink.written - consumed);
    }
    CHECK_EQUAL(0, sink.stats.realigned);
    CHECK_EQUAL(0, sink.stats.underruns);
    CHECK_EQUAL(0, sink.stats.overflows);
    CHECK_EQUAL(0, sink.stats.dropped);
    CHECK_EQUAL(0, sink.stats.repeated);
}

static void the_meter_measures_trimmed_samples_at_every_usb_width(void)
{
    for (uint8_t width = 2; width <= 4; width++) {
        UsbAudioSink sink = started_sink(width);
        usb_audio_sink_set_headphones(
            &sink, (HeadphoneAudio){.left = REPLAY_GAIN_UNITY / 4, .right = REPLAY_GAIN_UNITY / 2});
        uint8_t bytes[8] = {0};
        for (uint8_t index = 0; index < width; index++) {
            bytes[index] = index == width - 1 ? 0x7F : 0xFF;
        }
        bytes[2 * width - 1] = 0x80;
        usb_audio_sink_write(&sink, bytes, 2u * width, 0);
        uint16_t peaks[2];
        usb_audio_sink_take_peaks(&sink, peaks);
        CHECK_EQUAL(width == 2 ? 8191 : 8192, peaks[0]);
        CHECK_EQUAL(16384, peaks[1]);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_meter_measures_trimmed_samples_at_every_usb_width),
        TEST_CASE(a_two_millisecond_delivery_burst_stays_inside_the_192_khz_buffer),
        TEST_CASE(every_rate_and_width_streams_bit_exact_with_fast_and_slow_device_clocks),
        TEST_CASE(asynchronous_audio_never_drops_or_repeats_frames_to_correct_fill),
        TEST_CASE(feedback_tracks_the_output_clock_and_steers_the_host_toward_target_fill),
        TEST_CASE(thirty_two_bit_samples_preserve_every_bit_at_unity_gain),
        TEST_CASE(peaks_follow_the_loudest_sample_of_each_channel_until_taken),
        TEST_CASE(the_most_negative_sample_is_a_full_scale_peak),
        TEST_CASE(headphones_trim_both_usb_formats_and_survive_rate_restarts),
        TEST_CASE(sixteen_bit_samples_go_to_the_top_of_each_word),
        TEST_CASE(twenty_four_bit_samples_are_left_justified),
        TEST_CASE(a_partial_frame_at_the_end_of_a_packet_is_ignored),
        TEST_CASE(nothing_is_written_while_streaming_is_off),
        TEST_CASE(the_sink_is_primed_after_twenty_milliseconds),
        TEST_CASE(silence_is_kept_ahead_of_the_last_packet),
        TEST_CASE(a_full_buffer_while_playing_drops_one_frame_per_packet),
        TEST_CASE(a_low_buffer_while_playing_repeats_the_last_frame),
        TEST_CASE(no_frames_are_dropped_or_repeated_before_playing),
        TEST_CASE(an_underrun_restarts_twenty_milliseconds_ahead_of_the_output),
        TEST_CASE(an_overflow_also_restarts_near_the_output),
        TEST_CASE(writing_wraps_around_the_end_of_the_ring),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
