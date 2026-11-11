#include <math.h>
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "device/audio_resample.h"

enum {
    INPUT_CAPACITY = 5000,
    OUTPUT_CAPACITY = INPUT_CAPACITY * AUDIO_RESAMPLE_FACTOR_LIMIT,
    TONE_AMPLITUDE = 1000000,
    BLOCK_LIMIT = 7,
    SAMPLE_SHIFT = 8,
    SAMPLE_LOW_BITS = 0xFF
};

static AudioResample state;
static int32_t input[INPUT_CAPACITY];
static int32_t output[OUTPUT_CAPACITY];
static int32_t comparison[OUTPUT_CAPACITY];
static uint32_t used;

static uint32_t block_frames = 1;

static bool push_frame(int32_t left, int32_t right)
{
    const int32_t frame[2] = {left, right};
    return audio_resample_push(&state, frame, 1) == 1;
}

static void take_output(bool identical)
{
    uint32_t words[2 * BLOCK_LIMIT];
    uint32_t made = 0;
    while ((made = audio_resample_render(&state, words, block_frames)) > 0) {
        for (uint32_t frame = 0; frame < made; frame++) {
            CHECK(used < OUTPUT_CAPACITY);
            CHECK_EQUAL(0u, words[2 * frame] & SAMPLE_LOW_BITS);
            output[used++] = (int32_t)words[2 * frame] >> SAMPLE_SHIFT;
            CHECK_EQUAL(identical ? words[2 * frame] : 0u, words[2 * frame + 1]);
        }
    }
}

static uint32_t render_identical(uint32_t rate, uint32_t count)
{
    audio_resample_start(&state, rate);
    used = 0;
    for (uint32_t index = 0; index < count; index++) {
        if (!push_frame(input[index], input[index])) {
            take_output(true);
            CHECK(push_frame(input[index], input[index]));
        }
    }
    audio_resample_finish(&state);
    take_output(true);
    return used;
}

static uint32_t render(uint32_t rate, uint32_t count, uint32_t cadence)
{
    audio_resample_start(&state, rate);
    used = 0;
    for (uint32_t index = 0; index < count; index++) {
        if (!push_frame(input[index], 0)) {
            take_output(false);
            CHECK(push_frame(input[index], 0));
        }
        if (index % cadence == 0) {
            take_output(false);
        }
    }
    audio_resample_finish(&state);
    take_output(false);
    CHECK_EQUAL(0, audio_resample_pending(&state));
    return used;
}

static void every_speech_rate_has_an_integer_supported_output(void)
{
    static const uint32_t RATES[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000};
    static const uint32_t OUTPUTS[] = {32000, 44100, 48000, 32000, 44100, 48000, 32000};
    for (size_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
        CHECK_EQUAL(OUTPUTS[index], audio_resample_rate(RATES[index]));
    }
    CHECK_EQUAL(44100, audio_resample_rate(44100));
    CHECK_EQUAL(192000, audio_resample_rate(192000));
    CHECK_EQUAL(0, audio_resample_rate(0));
    CHECK_EQUAL(0, audio_resample_rate(13000));
}

static void native_rates_pass_each_sample_through_without_filtering(void)
{
    audio_resample_start(&state, 44100);
    CHECK(push_frame(-8388608, 8388607));
    uint32_t words[4] = {0};
    CHECK_EQUAL(1u, audio_resample_render(&state, words, 2));
    CHECK_EQUAL(0x80000000u, words[0]);
    CHECK_EQUAL(0x7FFFFF00u, words[1]);
    CHECK_EQUAL(0u, audio_resample_render(&state, words, 2));
}

static void short_streams_finish_at_exactly_the_input_duration(void)
{
    static const uint32_t RATES[] = {8000, 11025, 16000, 22050};
    for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
        uint32_t factor = audio_resample_rate(RATES[rate]) / RATES[rate];
        for (uint32_t count = 0; count < 80; count++) {
            CHECK_EQUAL(count * factor, render(RATES[rate], count, 7));
        }
    }
}

static void block_boundaries_and_backpressure_do_not_change_the_samples(void)
{
    static const uint32_t RATES[] = {8000, 16000};
    for (uint32_t index = 0; index < 500; index++) {
        input[index] = (int32_t)((index * 37193u) % 4000000u) - 2000000;
    }
    for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
        block_frames = 1;
        uint32_t first = render(RATES[rate], 500, 1);
        memcpy(comparison, output, first * sizeof output[0]);
        for (block_frames = 2; block_frames <= BLOCK_LIMIT; block_frames++) {
            CHECK_EQUAL(first, render(RATES[rate], 500, 47));
            CHECK(memcmp(comparison, output, first * sizeof output[0]) == 0);
        }
    }
    block_frames = 1;
}

static void a_block_push_accepts_only_the_free_history(void)
{
    int32_t frames[2 * AUDIO_RESAMPLE_HISTORY] = {0};
    audio_resample_start(&state, 16000);
    uint32_t room = audio_resample_room(&state);
    CHECK_EQUAL((uint32_t)(AUDIO_RESAMPLE_HISTORY - AUDIO_RESAMPLE_LEFT), room);
    CHECK_EQUAL(room, audio_resample_push(&state, frames, AUDIO_RESAMPLE_HISTORY));
    CHECK_EQUAL(0u, audio_resample_room(&state));
    CHECK_EQUAL(0u, audio_resample_push(&state, frames, 1));
    audio_resample_finish(&state);
    CHECK_EQUAL(0u, audio_resample_room(&state));
}

static void identical_channels_match_a_channel_filtered_beside_another(void)
{
    static const uint32_t RATES[] = {8000, 11025, 16000, 22050};
    for (uint32_t index = 0; index < 500; index++) {
        input[index] = (int32_t)((index * 48271u) % 16000000u) - 8000000;
    }
    for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
        uint32_t count = render(RATES[rate], 500, 3);
        memcpy(comparison, output, count * sizeof output[0]);
        CHECK_EQUAL(count, render_identical(RATES[rate], 500));
        CHECK(memcmp(comparison, output, count * sizeof output[0]) == 0);
    }
}

static void constant_samples_keep_unity_gain_in_every_phase(void)
{
    static const uint32_t RATES[] = {8000, 11025, 16000, 22050};
    for (int index = 0; index < INPUT_CAPACITY; index++) {
        input[index] = 1234567;
    }
    for (size_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
        uint32_t factor = audio_resample_rate(RATES[index]) / RATES[index];
        uint32_t count = render(RATES[index], 200, 1);
        for (uint32_t sample = 50 * factor; sample < count - 50 * factor; sample++) {
            CHECK_EQUAL(1234567, output[sample]);
        }
    }
}

static void wide_input_and_filter_overshoot_are_clipped_without_overflow(void)
{
    audio_resample_start(&state, 8000);
    for (int index = 0; index < 40; index++) {
        CHECK(push_frame(INT32_MAX, INT32_MIN));
    }
    audio_resample_finish(&state);
    uint32_t words[2];
    uint32_t count = 0;
    while (audio_resample_render(&state, words, 1) == 1) {
        CHECK_EQUAL(0u, words[0] & SAMPLE_LOW_BITS);
        CHECK_EQUAL(0u, words[1] & SAMPLE_LOW_BITS);
        count++;
    }
    CHECK_EQUAL(40 * audio_resample_rate(8000) / 8000, count);
}

static void restarting_discards_the_old_streams_history_and_pending_phases(void)
{
    for (uint32_t index = 0; index < 500; index++) {
        input[index] = 0;
    }
    uint32_t count = render(8000, 500, 1);
    memcpy(comparison, output, count * sizeof output[0]);
    audio_resample_start(&state, 8000);
    CHECK(push_frame(8000000, -8000000));
    CHECK_EQUAL(count, render(8000, 500, 7));
    CHECK(memcmp(comparison, output, count * sizeof output[0]) == 0);
    CHECK(!push_frame(1, 1));
}

typedef struct Tone {
    uint32_t factor;
    uint32_t count;
    double frequency;
} Tone;

static double tone_gain(const Tone *tone, double frequency, uint32_t edge)
{
    const double turn = 6.2831853071795864769;
    double sine = 0;
    double cosine = 0;
    for (uint32_t index = edge; index < tone->count - edge; index++) {
        double time = (double)index / tone->factor;
        sine += output[index] * sin(turn * frequency * time);
        cosine += output[index] * cos(turn * frequency * time);
    }
    return 2 * hypot(sine, cosine) / ((double)TONE_AMPLITUDE * (tone->count - 2 * edge));
}

static Tone render_tone(uint32_t rate, double frequency)
{
    const double turn = 6.2831853071795864769;
    for (int index = 0; index < INPUT_CAPACITY; index++) {
        input[index] = (int32_t)lround(TONE_AMPLITUDE * sin(turn * frequency * index));
    }
    uint32_t factor = audio_resample_rate(rate) / rate;
    return (Tone){factor, render(rate, INPUT_CAPACITY, 7), frequency};
}

static void converted_rates_have_flat_passband_and_reject_every_image(void)
{
    static const uint32_t RATES[] = {8000, 11025, 12000, 16000, 22050, 24000};
    static const double FREQUENCIES[] = {0.01, 0.1, 0.2, 0.3, 0.4};
    for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
        for (size_t index = 0; index < sizeof FREQUENCIES / sizeof FREQUENCIES[0]; index++) {
            Tone tone = render_tone(RATES[rate], FREQUENCIES[index]);
            uint32_t edge = 100 * tone.factor;
            CHECK(fabs(20 * log10(tone_gain(&tone, tone.frequency, edge))) < 0.01);
            for (uint32_t image = 1; image < tone.factor; image++) {
                CHECK(tone_gain(&tone, image - tone.frequency, edge) < 0.0001);
                CHECK(tone_gain(&tone, image + tone.frequency, edge) < 0.0001);
            }
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(every_speech_rate_has_an_integer_supported_output),
        TEST_CASE(native_rates_pass_each_sample_through_without_filtering),
        TEST_CASE(short_streams_finish_at_exactly_the_input_duration),
        TEST_CASE(block_boundaries_and_backpressure_do_not_change_the_samples),
        TEST_CASE(a_block_push_accepts_only_the_free_history),
        TEST_CASE(identical_channels_match_a_channel_filtered_beside_another),
        TEST_CASE(constant_samples_keep_unity_gain_in_every_phase),
        TEST_CASE(wide_input_and_filter_overshoot_are_clipped_without_overflow),
        TEST_CASE(restarting_discards_the_old_streams_history_and_pending_phases),
        TEST_CASE(converted_rates_have_flat_passband_and_reject_every_image),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
