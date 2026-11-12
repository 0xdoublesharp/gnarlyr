#include <string.h>

#include "check.h"
#include "device/ape_track.h"

static uint8_t bytes[1024];

static void put(uint32_t at, uint32_t value)
{
    for (uint32_t index = 0; index < 4; index++) {
        bytes[at + index] = (uint8_t)(value >> (index * 8));
    }
}

static bool read_bytes(void *context, uint32_t offset, void *out, uint32_t length)
{
    (void)context;
    if (offset > sizeof bytes || length > sizeof bytes - offset) {
        return false;
    }
    memcpy(out, bytes + offset, length);
    return true;
}

static FlacInput fixture(void)
{
    memset(bytes, 0, sizeof bytes);
    memcpy(bytes, "MAC ", 4);
    put(4, 3990);
    put(8, 52);
    put(12, 24);
    put(16, 12);
    put(20, 44);
    put(24, 812);
    put(52, 3000);
    put(56, 73728);
    put(60, 1234);
    put(64, 3);
    put(68, 16 | (2u << 16));
    put(72, 44100);
    put(76, 132);
    put(80, 401);
    put(84, 700);
    return (FlacInput){read_bytes, NULL, sizeof bytes};
}

static void probes_modern_headers_and_uses_word_aligned_seek_entries(void)
{
    ApeTrack track;
    FlacInput input = fixture();
    CHECK(ape_track_probe(input, &track));
    CHECK_EQUAL(3990, track.version);
    CHECK_EQUAL(3000, track.level);
    CHECK_EQUAL(148690, track.samples);
    CHECK_EQUAL(132, track.audio_start);
    CHECK_EQUAL(944, track.audio_end);
    ApeFrame frame;
    CHECK(ape_track_frame(input, &track, 1, &frame));
    CHECK_EQUAL(400, frame.offset);
    CHECK_EQUAL(1, frame.skip);
    CHECK_EQUAL(304, frame.bytes);
    CHECK_EQUAL(73728, frame.samples);
    CHECK(ape_track_frame(input, &track, 2, &frame));
    CHECK_EQUAL(1234, frame.samples);
    CHECK(!ape_track_frame(input, &track, 3, &frame));
}

static void refuses_damaged_bounds_without_replacing_the_result(void)
{
    static const uint32_t offsets[] = {8, 12, 16, 24, 28, 56, 60, 64, 68, 72, 76, 80, 84};
    ApeTrack track = {.rate = 17};
    ApeTrack before = track;
    for (uint32_t index = 0; index < sizeof offsets / sizeof offsets[0]; index++) {
        FlacInput input = fixture();
        put(offsets[index], UINT32_MAX);
        CHECK(!ape_track_probe(input, &track));
        CHECK(memcmp(&track, &before, sizeof track) == 0);
    }
    CHECK(!ape_track_probe(fixture(), NULL));
}

static void accepts_supported_depths_channels_and_rates(void)
{
    for (uint32_t bits = 16; bits <= 24; bits += 8) {
        for (uint32_t channels = 1; channels <= 2; channels++) {
            ApeTrack track;
            FlacInput input = fixture();
            put(68, bits | channels << 16);
            put(72, 192000);
            put(52, 1000);
            CHECK(ape_track_probe(input, &track));
            CHECK_EQUAL(bits, track.bits);
            CHECK_EQUAL(channels, track.channels);
            CHECK_EQUAL(192000, track.rate);
        }
    }
}

static void playback_limits_keep_a_fifth_of_the_requested_core_clock_in_reserve(void)
{
    static const struct {
        uint16_t level;
        uint16_t channels;
        uint32_t base;
        uint32_t boosted;
    } limits[] = {{1000, 1, 192000, 192000}, {1000, 2, 96000, 192000}, {2000, 1, 192000, 192000},
                  {2000, 2, 48000, 96000},   {3000, 1, 96000, 96000},  {3000, 2, 48000, 48000}};
    for (uint32_t index = 0; index < sizeof limits / sizeof limits[0]; index++) {
        ApeTrack track = {.level = limits[index].level,
                          .channels = limits[index].channels,
                          .bits = 24,
                          .rate = limits[index].base};
        CHECK_EQUAL(100, ape_track_clock(&track));
        CHECK(ape_track_realtime(&track, 100));
        CHECK(!ape_track_realtime(&track, 0));
        track.rate = limits[index].base + 1;
        CHECK_EQUAL(track.rate <= limits[index].boosted ? 200 : 0, ape_track_clock(&track));
        CHECK(!ape_track_realtime(&track, 100));
        CHECK_EQUAL(track.rate <= limits[index].boosted, ape_track_realtime(&track, 200));
        track.rate = limits[index].boosted + 1;
        CHECK_EQUAL(0, ape_track_clock(&track));
        CHECK(!ape_track_realtime(&track, 200));
    }
    ApeTrack extra = {.level = 4000, .channels = 1, .bits = 16, .rate = 8000};
    CHECK_EQUAL(0, ape_track_clock(&extra));
    CHECK(!ape_track_realtime(&extra, 200));
    extra.level = 5000;
    CHECK(!ape_track_realtime(&extra, 200));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(playback_limits_keep_a_fifth_of_the_requested_core_clock_in_reserve),
        TEST_CASE(probes_modern_headers_and_uses_word_aligned_seek_entries),
        TEST_CASE(refuses_damaged_bounds_without_replacing_the_result),
        TEST_CASE(accepts_supported_depths_channels_and_rates),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
