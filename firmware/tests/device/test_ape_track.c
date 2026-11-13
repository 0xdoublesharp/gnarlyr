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

static void put16(uint32_t at, uint32_t value)
{
    bytes[at] = (uint8_t)value;
    bytes[at + 1] = (uint8_t)(value >> 8);
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

static FlacInput legacy_fixture(uint16_t flags, uint32_t seeks)
{
    memset(bytes, 0, sizeof bytes);
    memcpy(bytes, "MAC ", 4);
    put16(4, 3970);
    put16(6, 3000);
    put16(8, flags);
    put16(10, 2);
    put(12, 44100);
    put(16, 4);
    put(20, 0);
    put(24, 2);
    put(28, 100);
    uint32_t header = 32 + ((flags & 4) != 0 ? 4u : 0u);
    if ((flags & 16) != 0) {
        put(header, seeks);
        header += 4;
    }
    uint32_t seek = header + 4;
    uint32_t start = seek + seeks * 4;
    put(seek, start);
    put(seek + 4, start + 16);
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

static void probes_legacy_headers_with_optional_fields_and_depths(void)
{
    static const uint16_t FLAGS[] = {0, 4, 8, 16, 4 | 8 | 16};
    for (uint32_t index = 0; index < sizeof FLAGS / sizeof FLAGS[0]; index++) {
        ApeTrack track;
        FlacInput input = legacy_fixture(FLAGS[index], 2);
        CHECK(ape_track_probe(input, &track));
        CHECK_EQUAL(3970, track.version);
        CHECK_EQUAL((FLAGS[index] & 8) != 0 ? 24 : 16, track.bits);
        CHECK_EQUAL(2, track.frames);
        CHECK_EQUAL(294912, track.frame_samples);
        CHECK_EQUAL(295012, track.samples);
        ApeFrame frame;
        CHECK(ape_track_frame(input, &track, 0, &frame));
        CHECK_EQUAL(track.audio_start, frame.offset);
        CHECK_EQUAL(294912, frame.samples);
        CHECK(ape_track_frame(input, &track, 1, &frame));
        CHECK_EQUAL(100, frame.samples);
    }
}

static void legacy_headers_reject_unsupported_and_short_seek_tables(void)
{
    ApeTrack track = {.rate = 17};
    ApeTrack before = track;
    CHECK(!ape_track_probe(legacy_fixture(1, 2), &track));
    CHECK(memcmp(&track, &before, sizeof track) == 0);
    CHECK(!ape_track_probe(legacy_fixture(16, 1), &track));
    CHECK(memcmp(&track, &before, sizeof track) == 0);
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
        TEST_CASE(probes_legacy_headers_with_optional_fields_and_depths),
        TEST_CASE(legacy_headers_reject_unsupported_and_short_seek_tables),
        TEST_CASE(refuses_damaged_bounds_without_replacing_the_result),
        TEST_CASE(accepts_supported_depths_channels_and_rates),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
