#include <math.h>
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "device/audio_resample.h"

enum {
    INPUT_CAPACITY = 5000,
    OUTPUT_CAPACITY = INPUT_CAPACITY * AUDIO_RESAMPLE_FACTOR_LIMIT,
    TONE_AMPLITUDE = 1000000
};

static AudioResample state;
static int32_t input[INPUT_CAPACITY];
static int32_t output[OUTPUT_CAPACITY];
static int32_t comparison[OUTPUT_CAPACITY];
static uint32_t used;

static void take_identical_output(void)
{
    int32_t left = 0;
    int32_t right = 0;
    while (audio_resample_pull(&state, &left, &right)) {
        CHECK(used < OUTPUT_CAPACITY);
        CHECK_EQUAL(left, right);
        output[used++] = left;
    }
}

static uint32_t render_identical(uint32_t rate, uint32_t count)
{
    audio_resample_start(&state, rate);
    used = 0;
    for (uint32_t index = 0; index < count; index++) {
        if (!audio_resample_push(&state, input[index], input[index])) {
            take_identical_output();
            CHECK(audio_resample_push(&state, input[index], input[index]));
        }
    }
    audio_resample_finish(&state);
    take_identical_output();
    return used;
}

static void take_output(bool silent_right)
{
    int32_t left = 0;
    int32_t right = 0;
    while (audio_resample_pull(&state, &left, &right)) {
        CHECK(used < OUTPUT_CAPACITY);
        output[used++] = left;
        if (silent_right) {
            CHECK_EQUAL(0, right);
        } else {
            CHECK_EQUAL(-left, right);
        }
    }
}

static uint32_t render(uint32_t rate, uint32_t count, uint32_t cadence)
{
    audio_resample_start(&state, rate);
    used = 0;
    for (uint32_t index = 0; index < count; index++) {
        if (!audio_resample_push(&state, input[index], 0)) {
            take_output(true);
            CHECK(audio_resample_push(&state, input[index], 0));
        }
        if (index % cadence == 0) {
            take_output(true);
        }
    }
    audio_resample_finish(&state);
    take_output(true);
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

static void native_rates_preserve_every_sample_without_filtering(void)
{
    audio_resample_start(&state, 44100);
    CHECK(audio_resample_push(&state, INT32_MIN, INT32_MAX));
    int32_t left = 0;
    int32_t right = 0;
    CHECK(audio_resample_pull(&state, &left, &right));
    CHECK_EQUAL(INT32_MIN, left);
    CHECK_EQUAL(INT32_MAX, right);
    CHECK(!audio_resample_pull(&state, &left, &right));
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
    for (uint32_t index = 0; index < 500; index++) {
        input[index] = (int32_t)((index * 37193u) % 4000000u) - 2000000;
    }
    uint32_t first = render(8000, 500, 1);
    memcpy(comparison, output, first * sizeof output[0]);
    CHECK_EQUAL(first, render(8000, 500, 47));
    CHECK(memcmp(comparison, output, first * sizeof output[0]) == 0);
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
        CHECK(audio_resample_push(&state, INT32_MAX, INT32_MIN));
    }
    audio_resample_finish(&state);
    int32_t left = 0;
    int32_t right = 0;
    uint32_t count = 0;
    while (audio_resample_pull(&state, &left, &right)) {
        CHECK(left >= -8388608 && left <= 8388607);
        CHECK(right >= -8388608 && right <= 8388607);
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
    CHECK(audio_resample_push(&state, 8000000, -8000000));
    CHECK_EQUAL(count, render(8000, 500, 7));
    CHECK(memcmp(comparison, output, count * sizeof output[0]) == 0);
    CHECK(!audio_resample_push(&state, 1, 1));
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
        TEST_CASE(native_rates_preserve_every_sample_without_filtering),
        TEST_CASE(short_streams_finish_at_exactly_the_input_duration),
        TEST_CASE(block_boundaries_and_backpressure_do_not_change_the_samples),
        TEST_CASE(identical_channels_match_a_channel_filtered_beside_another),
        TEST_CASE(constant_samples_keep_unity_gain_in_every_phase),
        TEST_CASE(wide_input_and_filter_overshoot_are_clipped_without_overflow),
        TEST_CASE(restarting_discards_the_old_streams_history_and_pending_phases),
        TEST_CASE(converted_rates_have_flat_passband_and_reject_every_image),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
