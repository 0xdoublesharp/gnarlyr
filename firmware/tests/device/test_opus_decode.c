#include "device/opus_decode.h"

#include <string.h>

#include "check.h"

static _Alignas(8) uint8_t state[32768];
static _Alignas(8) uint8_t scratch[OPUS_DECODE_SCRATCH_BYTES + 16];
static int16_t samples[OPUS_DECODE_MAX_FRAMES * 2 + 2];
static OpusDecode decoder;

static bool start(uint32_t rate, uint32_t channels, uint32_t bytes)
{
    memset(scratch, 0xA5, sizeof scratch);
    return opus_decode_init(&decoder, state, sizeof state, scratch + 8, bytes, rate, channels) ==
           OPUS_DECODE_OK;
}

static OpusDecodeResult decode(const uint8_t *packet, uint32_t bytes, uint32_t frames, bool fec,
                               OpusDecodePcm *pcm)
{
    return opus_decode_packet(&decoder, (OpusDecodeInput){packet, bytes, frames, fec}, pcm);
}

static void canaries(void)
{
    for (uint32_t index = 0; index < 8; index++) {
        CHECK_EQUAL(0xA5, scratch[index]);
        CHECK_EQUAL(0xA5, scratch[OPUS_DECODE_SCRATCH_BYTES + 8 + index]);
    }
}

static void silence_packets_cover_all_modes_and_output_rates(void)
{
    static const uint32_t rates[] = {8000, 12000, 16000, 24000, 48000};
    static const uint8_t packets[][3] = {{0x08, 0, 0}, {0x68, 0, 0}, {0xF8, 0xFF, 0xFE}};
    static const OpusDecodeMode modes[] = {OPUS_DECODE_SILK, OPUS_DECODE_HYBRID, OPUS_DECODE_CELT};
    for (uint32_t rate = 0; rate < 5; rate++) {
        for (uint32_t channels = 1; channels <= 2; channels++) {
            for (uint32_t mode = 0; mode < 3; mode++) {
                CHECK(start(rates[rate], channels, OPUS_DECODE_SCRATCH_BYTES));
                OpusDecodePcm pcm = {.samples = samples, .capacity = OPUS_DECODE_MAX_FRAMES};
                CHECK_EQUAL(OPUS_DECODE_OK, decode(packets[mode], 3, 0, false, &pcm));
                CHECK_EQUAL(rates[rate] / 50, pcm.frames);
                CHECK_EQUAL(rates[rate], pcm.rate);
                CHECK_EQUAL(channels, pcm.channels);
                CHECK_EQUAL(modes[mode], pcm.mode);
                CHECK(decoder.scratch_peak <= OPUS_DECODE_SCRATCH_BYTES);
                CHECK_EQUAL(0, decoder.scratch_used);
                canaries();
            }
        }
    }
}

static void a_120ms_packet_fits_the_documented_interleaved_output(void)
{
    static const uint8_t packet[] = {0xFB, 6,    0xFF, 0xFE, 0xFF, 0xFE, 0xFF,
                                     0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE};
    CHECK(start(48000, 2, OPUS_DECODE_SCRATCH_BYTES));
    samples[OPUS_DECODE_MAX_FRAMES * 2] = 12345;
    OpusDecodePcm pcm = {.samples = samples, .capacity = OPUS_DECODE_MAX_FRAMES};
    CHECK_EQUAL(OPUS_DECODE_OK, decode(packet, sizeof packet, 0, false, &pcm));
    CHECK_EQUAL(5760, pcm.frames);
    CHECK_EQUAL(12345, samples[OPUS_DECODE_MAX_FRAMES * 2]);
    canaries();
}

static void loss_and_fec_have_explicit_requested_durations(void)
{
    static const uint8_t packet[] = {0x48, 0, 0};
    CHECK(start(48000, 2, OPUS_DECODE_SCRATCH_BYTES));
    OpusDecodePcm pcm = {.samples = samples, .capacity = OPUS_DECODE_MAX_FRAMES};
    CHECK_EQUAL(OPUS_DECODE_OK, decode(packet, sizeof packet, 0, false, &pcm));
    CHECK_EQUAL(OPUS_DECODE_OK, decode(NULL, 0, 5760, false, &pcm));
    CHECK_EQUAL(5760, pcm.frames);
    CHECK_EQUAL(OPUS_DECODE_OK, decode(packet, sizeof packet, 960, true, &pcm));
    CHECK_EQUAL(960, pcm.frames);
    CHECK_EQUAL(OPUS_DECODE_INVALID, decode(NULL, 0, 0, false, &pcm));
    CHECK_EQUAL(OPUS_DECODE_INVALID, decode(packet, sizeof packet, 0, true, &pcm));
    canaries();
}

static void short_output_is_refused_before_state_or_samples_change(void)
{
    static const uint8_t packet[] = {0xF8, 0xFF, 0xFE};
    static uint8_t saved[sizeof state];
    CHECK(start(48000, 2, OPUS_DECODE_SCRATCH_BYTES));
    memcpy(saved, state, sizeof state);
    samples[0] = 12345;
    OpusDecodePcm pcm = {.samples = samples, .capacity = 959};
    CHECK_EQUAL(OPUS_DECODE_BUFFER_SMALL, decode(packet, sizeof packet, 0, false, &pcm));
    CHECK_EQUAL(0, pcm.frames);
    CHECK_EQUAL(12345, samples[0]);
    CHECK_EQUAL(0, memcmp(saved, state, sizeof state));
    CHECK(decoder.ready);
}

static void exhausted_scratch_returns_an_error_and_can_be_reinitialized(void)
{
    static const uint8_t packet[] = {0xF8, 0xFF, 0xFE};
    CHECK(start(48000, 2, 8));
    OpusDecodePcm pcm = {.samples = samples, .capacity = OPUS_DECODE_MAX_FRAMES};
    CHECK_EQUAL(OPUS_DECODE_EXHAUSTED, decode(packet, sizeof packet, 0, false, &pcm));
    CHECK_EQUAL(0, pcm.frames);
    CHECK(!decoder.ready);
    CHECK_EQUAL(OPUS_DECODE_FAILED, decode(packet, sizeof packet, 0, false, &pcm));
    CHECK(start(48000, 2, OPUS_DECODE_SCRATCH_BYTES));
    CHECK_EQUAL(OPUS_DECODE_OK, decode(packet, sizeof packet, 0, false, &pcm));
    canaries();
}

static void invalid_layouts_never_become_ready(void)
{
    CHECK_EQUAL(0, opus_decode_state_bytes(0));
    CHECK_EQUAL(0, opus_decode_state_bytes(3));
    CHECK(opus_decode_state_bytes(1) > 0);
    CHECK(opus_decode_state_bytes(2) > opus_decode_state_bytes(1));
    CHECK_EQUAL(OPUS_DECODE_INVALID, opus_decode_init(&decoder, state + 1, sizeof state - 1,
                                                      scratch, sizeof scratch, 48000, 2));
    CHECK_EQUAL(OPUS_DECODE_INVALID,
                opus_decode_init(&decoder, state, 8, scratch, sizeof scratch, 48000, 2));
    CHECK_EQUAL(OPUS_DECODE_INVALID,
                opus_decode_init(&decoder, state, sizeof state, state, sizeof state, 48000, 2));
    CHECK_EQUAL(OPUS_DECODE_INVALID,
                opus_decode_init(&decoder, state, sizeof state, scratch, sizeof scratch, 44100, 2));
    CHECK(!decoder.ready);
}

static void metadata_and_output_cannot_alias_decoder_storage(void)
{
    static const uint8_t packet[] = {0xF8, 0xFF, 0xFE};
    static uint8_t saved[sizeof state];
    CHECK(start(48000, 2, OPUS_DECODE_SCRATCH_BYTES));
    memcpy(saved, state, sizeof state);
    OpusDecodePcm *aliased = (OpusDecodePcm *)(void *)state;
    CHECK_EQUAL(
        OPUS_DECODE_INVALID,
        opus_decode_packet(&decoder, (OpusDecodeInput){packet, sizeof packet, 0, false}, aliased));
    CHECK_EQUAL(0, memcmp(saved, state, sizeof state));
    OpusDecodePcm pcm = {.samples = (int16_t *)(void *)state, .capacity = 5760};
    CHECK_EQUAL(OPUS_DECODE_INVALID, decode(packet, sizeof packet, 0, false, &pcm));
    CHECK_EQUAL(0, memcmp(saved, state, sizeof state));
}

static uint32_t next_random(uint32_t *state_value)
{
    uint32_t value = *state_value;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state_value = value;
    return value;
}

static void malformed_packets_and_mode_transitions_keep_a_32k_arena_bounded(void)
{
    static uint8_t packet[1275];
    static const uint32_t rates[] = {8000, 12000, 16000, 24000, 48000};
    static const uint32_t lengths[] = {1, 2, 3, 16, 1275};
    uint32_t random_state = 6716;
    uint32_t peak = 0;
    for (uint32_t rate = 0; rate < 5; rate++) {
        for (uint32_t channels = 1; channels <= 2; channels++) {
            CHECK(start(rates[rate], channels, 32768));
            for (uint32_t toc = 0; toc < 256; toc++) {
                for (uint32_t size = 0; size < 5; size++) {
                    if (!decoder.ready) {
                        CHECK(start(rates[rate], channels, 32768));
                    }
                    packet[0] = (uint8_t)toc;
                    for (uint32_t byte = 1; byte < lengths[size]; byte++) {
                        packet[byte] = (uint8_t)next_random(&random_state);
                    }
                    OpusDecodePcm pcm = {.samples = samples, .capacity = OPUS_DECODE_MAX_FRAMES};
                    bool fec = size % 2 != 0;
                    OpusDecodeResult result =
                        decode(packet, lengths[size], fec ? rates[rate] / 50 : 0, fec, &pcm);
                    CHECK(result == OPUS_DECODE_OK || result == OPUS_DECODE_INVALID);
                    CHECK(decoder.scratch_peak <= 32768);
                    CHECK_EQUAL(0, decoder.scratch_used);
                    if (decoder.scratch_peak > peak) {
                        peak = decoder.scratch_peak;
                    }
                    if (result == OPUS_DECODE_OK) {
                        CHECK(pcm.frames > 0 && pcm.frames <= rates[rate] * 3 / 25);
                        CHECK_EQUAL(rates[rate], pcm.rate);
                        CHECK_EQUAL(channels, pcm.channels);
                    }
                    for (uint32_t byte = 32768 + 8; byte < sizeof scratch; byte++) {
                        CHECK_EQUAL(0xA5, scratch[byte]);
                    }
                    canaries();
                }
            }
        }
    }
    CHECK(peak > 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(silence_packets_cover_all_modes_and_output_rates),
        TEST_CASE(a_120ms_packet_fits_the_documented_interleaved_output),
        TEST_CASE(loss_and_fec_have_explicit_requested_durations),
        TEST_CASE(short_output_is_refused_before_state_or_samples_change),
        TEST_CASE(exhausted_scratch_returns_an_error_and_can_be_reinitialized),
        TEST_CASE(invalid_layouts_never_become_ready),
        TEST_CASE(metadata_and_output_cannot_alias_decoder_storage),
        TEST_CASE(malformed_packets_and_mode_transitions_keep_a_32k_arena_bounded),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
