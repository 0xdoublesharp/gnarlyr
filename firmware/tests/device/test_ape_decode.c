#include <stdio.h>
#include <stdlib.h>

#include "check.h"
#include "device/ape_stream.h"
#include "device/core1_ape.h"
#include "m4a_fixture_files.h"

static Core1Ape ape;
static uint8_t snapshot[APE_INPUT_BYTES];
static uint8_t records[524288];

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
    Core1Decoder decoder = core1_ape_decoder(&ape, snapshot);
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
    CHECK_EQUAL(track.samples, position);
    CHECK(chunks > 0);
    m4a_file_free(&file);
}

static void all_levels_match_the_independent_reference_sample_for_sample(void)
{
    static const char *const names[] = {"fast16",       "normal24",       "high16",
                                        "extra16",      "insane16",       "normal16",
                                        "fast24stereo", "normal24stereo", "high24stereo"};
    for (uint32_t index = 0; index < sizeof names / sizeof names[0]; index++) {
        reference(names[index], 0);
    }
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
    Core1Decoder decoder = core1_ape_decoder(&ape, snapshot);
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
        TEST_CASE(sequential_frames_and_seek_table_restarts_are_exact),
        TEST_CASE(starvation_waits_and_corrupt_input_fails_within_the_snapshot),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
