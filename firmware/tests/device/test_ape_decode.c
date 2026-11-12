#include <stdio.h>
#include <stdlib.h>

#include "check.h"
#include "device/ape_stream.h"
#include "device/core1_ape.h"
#include "m4a_fixture_files.h"

static Core1Ape ape;
static uint8_t snapshot[APE_INPUT_BYTES];
static uint8_t records[524288];
static uint32_t requested_mhz;

static void record_clock(uint32_t mhz)
{
    requested_mhz = mhz;
}

static Core1Decoder recording_decoder(void)
{
    requested_mhz = 0;
    return core1_ape_decoder(&ape, snapshot, (StreamRegion){.request_clock = record_clock});
}

static bool read_records(void *context, uint32_t offset, void *out, uint32_t length)
{
    const uint32_t *available = context;
    if (offset > *available || length > *available - offset) {
        return false;
    }
    for (uint32_t index = 0; index < length; index++) {
        ((uint8_t *)out)[index] = records[offset + index];
    }
    return true;
}

static int32_t expected(uint32_t frame, uint32_t channel, uint32_t bits)
{
    int32_t value = (int32_t)((frame * (channel == 0 ? 31u : 47u)) % 30001u) - 15000;
    return bits == 24 ? value * 257 : value;
}

static void reference(const char *name, uint32_t first_frame)
{
    char path[128];
    (void)snprintf(path, sizeof path, "ape/%s.ape", name);
    M4aFile file = m4a_file_load(path);
    CHECK(file.size > 0);
    ApeTrack track;
    FlacInput input = m4a_file_input(&file);
    CHECK(ape_track_probe(input, &track));
    ApeStream adapter;
    CHECK(ape_stream_start(&adapter, input, &track, first_frame));
    uint32_t size = 0;
    while ((!adapter.ended || adapter.record_position < APE_RECORD_BYTES) &&
           size + 37 < sizeof records) {
        CHECK(ape_stream_read(&adapter, size, records + size, 37));
        size += 37;
    }
    CHECK(adapter.ended);
    Core1Decoder decoder = recording_decoder();
    bool starved = false;
    decoder.start(decoder.context, (FlacInput){read_records, &size, size}, (FlacInfo){0},
                  first_frame * track.frame_samples, &starved);
    uint32_t position = first_frame * track.frame_samples;
    Core1DecodeResult result;
    Core1Block block;
    uint32_t chunks = 0;
    while ((result = decoder.decode(decoder.context, &block)) == CORE1_DECODED) {
        CHECK_EQUAL(position, block.first_sample);
        CHECK(block.samples > 0 && block.samples <= APE_CHUNK_SAMPLES);
        for (uint32_t sample = 0; sample < block.samples; sample++) {
            for (uint32_t channel = 0; channel < track.channels; channel++) {
                if (expected(position + sample, channel, track.bits) !=
                    block.channels[channel][sample]) {
                    CHECK_EQUAL(expected(position + sample, channel, track.bits),
                                block.channels[channel][sample]);
                    m4a_file_free(&file);
                    return;
                }
            }
        }
        position += block.samples;
        chunks++;
    }
    CHECK_EQUAL(CORE1_FINISHED, result);
    CHECK_EQUAL(ape_track_clock(&track) == APE_LIMIT_BOOST_MHZ ? 200 : 100, requested_mhz);
    CHECK_EQUAL(track.samples, position);
    CHECK(chunks > 0);
    m4a_file_free(&file);
}

static void all_levels_match_the_independent_reference_sample_for_sample(void)
{
    static const char *const names[] = {"fast16",         "normal24",         "high16",
                                        "extra16",        "insane16",         "normal16",
                                        "fast24stereo",   "normal24stereo",   "high24stereo",
                                        "fast24stereo96", "normal24stereo96", "high24mono96"};
    for (uint32_t index = 0; index < sizeof names / sizeof names[0]; index++) {
        reference(names[index], 0);
    }
}

static uint32_t clock_for(const char *name)
{
    char path[128];
    (void)snprintf(path, sizeof path, "ape/%s.ape", name);
    M4aFile file = m4a_file_load(path);
    ApeTrack track;
    bool probed = file.size > 0 && ape_track_probe(m4a_file_input(&file), &track);
    m4a_file_free(&file);
    return probed ? ape_track_clock(&track) : 1;
}

static void only_measured_streams_above_the_base_budget_request_the_boost(void)
{
    static const struct {
        const char *name;
        uint32_t clock;
    } cases[] = {{"fast16", 100},       {"normal16", 100},
                 {"high16", 100},       {"normal24", 100},
                 {"high24mono96", 100}, {"fast24stereo96", 100},
                 {"fast24stereo", 200}, {"normal24stereo96", 200},
                 {"normal24stereo", 0}, {"high24stereo", 0},
                 {"extra16", 0},        {"insane16", 0}};
    for (uint32_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        CHECK_EQUAL(cases[index].clock, clock_for(cases[index].name));
    }
    reference("normal24stereo96", 0);
    CHECK_EQUAL(200, requested_mhz);
    reference("fast16", 0);
    CHECK_EQUAL(100, requested_mhz);
}

static void sequential_frames_and_seek_table_restarts_are_exact(void)
{
    reference("seek16", 0);
    reference("seek16", 1);
}

static void starvation_waits_and_corrupt_input_fails_within_the_snapshot(void)
{
    M4aFile file = m4a_file_load("ape/high16.ape");
    FlacInput input = m4a_file_input(&file);
    ApeTrack track;
    ApeStream adapter;
    CHECK(ape_track_probe(input, &track));
    CHECK(ape_stream_start(&adapter, input, &track, 0));
    uint32_t size = 0;
    while (!adapter.ended || adapter.record_position < APE_RECORD_BYTES) {
        CHECK(ape_stream_read(&adapter, size, records + size, 37));
        size += 37;
    }
    uint32_t available = 0;
    bool starved = false;
    Core1Block block;
    Core1Decoder decoder = recording_decoder();
    decoder.start(decoder.context, (FlacInput){read_records, &available, size}, (FlacInfo){0}, 0,
                  &starved);
    for (uint32_t count = 0; count < 5; count++) {
        CHECK_EQUAL(CORE1_STARVED, decoder.decode(decoder.context, &block));
    }
    available = size;
    CHECK_EQUAL(CORE1_DECODED, decoder.decode(decoder.context, &block));
    CHECK_EQUAL(0, block.first_sample);
    CHECK_EQUAL(expected(0, 0, 16), block.channels[0][0]);
    records[APE_CONFIG_BYTES + APE_RECORD_BYTES + 12] ^= 0x55;
    decoder.start(decoder.context, (FlacInput){read_records, &available, size}, (FlacInfo){0}, 0,
                  &starved);
    Core1DecodeResult result = CORE1_DECODED;
    for (uint32_t count = 0; count < 30 && result == CORE1_DECODED; count++) {
        result = decoder.decode(decoder.context, &block);
    }
    CHECK_EQUAL(CORE1_FAILED, result);
    m4a_file_free(&file);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(all_levels_match_the_independent_reference_sample_for_sample),
        TEST_CASE(only_measured_streams_above_the_base_budget_request_the_boost),
        TEST_CASE(sequential_frames_and_seek_table_restarts_are_exact),
        TEST_CASE(starvation_waits_and_corrupt_input_fails_within_the_snapshot),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
