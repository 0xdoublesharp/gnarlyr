#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "device/audio_tempo.h"

enum {
    INPUT_CAPACITY = 96000,
    OUTPUT_CAPACITY = 2 * INPUT_CAPACITY,
    BANK_FRAMES = 3001
};

static AudioTempo state;
static uint8_t first[BANK_FRAMES * AUDIO_TEMPO_FRAME_BYTES];
static uint8_t second[BANK_FRAMES * AUDIO_TEMPO_FRAME_BYTES];
static int32_t input[INPUT_CAPACITY][2];
static int32_t output[OUTPUT_CAPACITY][2];
static uint32_t used;

static AudioTempoConfig config(uint32_t rate, uint16_t speed)
{
    return (AudioTempoConfig){
        .buffer = {first, second, BANK_FRAMES, BANK_FRAMES}, .rate = rate, .speed = speed};
}

static void take_output(void)
{
    int32_t left, right;
    while (audio_tempo_pull(&state, &left, &right)) {
        CHECK(used < OUTPUT_CAPACITY);
        output[used][0] = left;
        output[used++][1] = right;
    }
}

static uint32_t render_config(AudioTempoConfig setup, uint32_t frames, uint32_t cadence)
{
    CHECK(audio_tempo_start(&state, setup));
    used = 0;
    for (uint32_t index = 0; index < frames; index++) {
        if (!audio_tempo_push(&state, input[index][0], input[index][1])) {
            take_output();
            CHECK(audio_tempo_push(&state, input[index][0], input[index][1]));
        }
        if (index % cadence == 0) {
            take_output();
        }
    }
    audio_tempo_finish(&state);
    take_output();
    CHECK(audio_tempo_done(&state));
    return used;
}

static uint32_t render(uint32_t rate, uint16_t speed, uint32_t frames, uint32_t cadence)
{
    return render_config(config(rate, speed), frames, cadence);
}

static void unity_preserves_every_signed_24_bit_sample_and_stereo_channel(void)
{
    static const int32_t VALUES[] = {-8388608, 8388607, -1, 0, 1, -1200000, 3412345};
    for (uint32_t index = 0; index < 7000; index++) {
        input[index][0] = VALUES[index % 7];
        input[index][1] = VALUES[(index + 3) % 7];
    }
    CHECK_EQUAL(7000, render(48000, 100, 7000, 997));
    CHECK(memcmp(input, output, 7000 * sizeof input[0]) == 0);
}

static void slower_and_faster_streams_have_the_requested_duration(void)
{
    static const uint16_t SPEEDS[] = {50, 75, 90, 110, 125, 150, 175, 200};
    memset(input, 0, sizeof input);
    for (size_t index = 0; index < sizeof SPEEDS / sizeof SPEEDS[0]; index++) {
        uint16_t speed = SPEEDS[index];
        CHECK_EQUAL((32000u * 100 + speed / 2) / speed, render(16000, speed, 32000, 17));
    }
}

static void fill_tone(uint32_t rate, double hz, uint32_t frames)
{
    for (uint32_t index = 0; index < frames; index++) {
        int32_t sample = (int32_t)lround(2000000 * sin(6.283185307179586 * hz * index / rate));
        input[index][0] = sample;
        input[index][1] = -sample;
    }
}

static double frequency_between(uint32_t rate, uint32_t begin, uint32_t end)
{
    uint32_t first_cross = 0, last_cross = 0, count = 0;
    for (uint32_t index = begin + 1; index < end; index++) {
        if (output[index - 1][0] <= 0 && output[index][0] > 0) {
            first_cross = count == 0 ? index : first_cross;
            last_cross = index;
            count++;
        }
    }
    CHECK(count > 3);
    return (double)rate * (count - 1) / (last_cross - first_cross);
}

static double frequency(uint32_t rate, uint32_t frames)
{
    return frequency_between(rate, rate / 5, frames - rate / 5);
}

static void changing_duration_preserves_pitch_and_stereo_phase(void)
{
    static const uint32_t RATES[] = {8000, 16000, 44100, 48000};
    static const uint16_t SPEEDS[] = {50, 75, 125, 150, 200};
    static const double TONES[] = {20, 30, 40, 55, 110, 220, 440, 1000};
    double worst = 0, worst_tone = 0;
    uint32_t worst_rate = 0;
    uint16_t worst_speed = 0;
    for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
        for (size_t tone = 0; tone < sizeof TONES / sizeof TONES[0]; tone++) {
            fill_tone(RATES[rate], TONES[tone], 2 * RATES[rate]);
            for (size_t speed = 0; speed < sizeof SPEEDS / sizeof SPEEDS[0]; speed++) {
                uint32_t frames = render(RATES[rate], SPEEDS[speed], 2 * RATES[rate], 173);
                double measured = frequency(RATES[rate], frames);
                double error = fabs(measured - TONES[tone]) / TONES[tone];
                if (error > worst) {
                    worst = error;
                    worst_rate = RATES[rate];
                    worst_tone = TONES[tone];
                    worst_speed = SPEEDS[speed];
                }
                CHECK(error < 0.003);
                for (uint32_t index = 0; index < frames; index++) {
                    CHECK_EQUAL(-output[index][0], output[index][1]);
                }
            }
        }
    }
    printf("pitch matrix: 160 cases, maximum error %.5f%% at %u Hz, %.1f Hz tone, speed %u\n",
           100 * worst, worst_rate, worst_tone, worst_speed);
}

static void input_batches_and_output_backpressure_do_not_change_pcm(void)
{
    static int32_t reference[16000][2];
    fill_tone(16000, 137, 12000);
    uint32_t count = render(16000, 75, 12000, 1);
    CHECK_EQUAL(16000, count);
    memcpy(reference, output, count * sizeof output[0]);
    CHECK_EQUAL(count, render(16000, 75, 12000, 2001));
    CHECK(memcmp(reference, output, count * sizeof output[0]) == 0);
}

static void empty_and_subperiod_streams_end_at_the_rounded_duration(void)
{
    for (uint16_t speed = 50; speed <= 200; speed += 25) {
        for (uint32_t count = 0; count < 41; count++) {
            for (uint32_t index = 0; index < count; index++) {
                input[index][0] = 1234567;
                input[index][1] = -7654321;
            }
            uint32_t frames = render(48000, speed, count, 13);
            CHECK_EQUAL((count * 100 + speed / 2) / speed, frames);
            for (uint32_t index = 0; index < frames; index++) {
                CHECK_EQUAL(1234567, output[index][0]);
                CHECK_EQUAL(-7654321, output[index][1]);
            }
            int32_t left = 17, right = 19;
            CHECK(!audio_tempo_pull(&state, &left, &right));
            CHECK_EQUAL(17, left);
            CHECK_EQUAL(19, right);
            CHECK(!audio_tempo_push(&state, 0, 0));
        }
    }
}

static void invalid_configuration_preserves_the_existing_stream(void)
{
    CHECK(audio_tempo_start(&state, config(48000, 75)));
    CHECK(audio_tempo_push(&state, 12345, -54321));
    AudioTempo before = state;
    AudioTempoConfig bad = config(48000, 75);
    bad.buffer.second = first + 1;
    CHECK(!audio_tempo_start(&state, bad));
    CHECK(memcmp(&before, &state, sizeof state) == 0);
    bad = config(48000, 49);
    CHECK(!audio_tempo_start(&state, bad));
    bad = config(48000, 201);
    CHECK(!audio_tempo_start(&state, bad));
    bad = config(192000, 75);
    CHECK(!audio_tempo_start(&state, bad));
    bad = config(48000, 75);
    bad.buffer.first = NULL;
    CHECK(!audio_tempo_start(&state, bad));
    bad = config(48000, 75);
    bad.buffer.first_frames = 3;
    CHECK(!audio_tempo_start(&state, bad));
    bad = config(48000, 75);
    bad.buffer = (AudioTempoBuffer){(uint8_t *)&state, NULL, 2 * BANK_FRAMES, 0};
    CHECK(!audio_tempo_start(&state, bad));
    CHECK(memcmp(&before, &state, sizeof state) == 0);
}

static void backpressure_and_invalid_pcm_do_not_consume_input(void)
{
    CHECK(audio_tempo_start(&state, config(48000, 100)));
    for (uint32_t index = 0; index < 2 * BANK_FRAMES; index++) {
        CHECK(audio_tempo_push(&state, (int32_t)index, -(int32_t)index));
    }
    AudioTempo before = state;
    CHECK(!audio_tempo_push(&state, 0, 0));
    CHECK(memcmp(&before, &state, sizeof state) == 0);
    int32_t left, right;
    CHECK(audio_tempo_pull(&state, &left, &right));
    before = state;
    CHECK(!audio_tempo_push(&state, 8388608, 0));
    CHECK(!audio_tempo_push(&state, 0, INT32_MIN));
    CHECK(memcmp(&before, &state, sizeof state) == 0);
    CHECK(audio_tempo_push(&state, -8388608, 8388607));
}

static void zero_tiny_and_adjacent_spans_preserve_wrapping_pcm(void)
{
    static uint8_t workspace[2 * BANK_FRAMES * AUDIO_TEMPO_FRAME_BYTES];
    static const uint16_t SPLITS[] = {0, 1, 127, BANK_FRAMES, 2 * BANK_FRAMES - 1, 2 * BANK_FRAMES};
    for (uint32_t index = 0; index < 7000; index++) {
        input[index][0] = (int32_t)((index * 39133) % 16777216) - 8388608;
        input[index][1] = (int32_t)((index * 61933) % 16777216) - 8388608;
    }
    for (size_t index = 0; index < sizeof SPLITS / sizeof SPLITS[0]; index++) {
        uint16_t split = SPLITS[index];
        AudioTempoConfig setup = config(48000, 100);
        setup.buffer = (AudioTempoBuffer){split != 0 ? workspace : NULL,
                                          split != 2 * BANK_FRAMES ? workspace + split * 6 : NULL,
                                          split, (uint16_t)(2 * BANK_FRAMES - split)};
        CHECK_EQUAL(7000, render_config(setup, 7000, 1023));
        CHECK(memcmp(input, output, 7000 * sizeof input[0]) == 0);
    }
}

static void every_integer_speed_keeps_constant_pcm_and_exact_duration(void)
{
    for (uint32_t index = 0; index < 1501; index++) {
        input[index][0] = 8388607;
        input[index][1] = -8388608;
    }
    for (uint16_t speed = 50; speed <= 200; speed++) {
        uint32_t frames = render(8000, speed, 1501, 319);
        CHECK_EQUAL((150100u + speed / 2) / speed, frames);
        for (uint32_t index = 0; index < frames; index++) {
            CHECK_EQUAL(8388607, output[index][0]);
            CHECK_EQUAL(-8388608, output[index][1]);
        }
    }
}

static void an_unstarted_stream_can_be_drained_and_finished(void)
{
    AudioTempo empty = {0};
    int32_t left = 17, right = 19;
    CHECK(!audio_tempo_push(&empty, 0, 0));
    CHECK(!audio_tempo_pull(&empty, &left, &right));
    audio_tempo_finish(&empty);
    CHECK(audio_tempo_done(&empty));
    CHECK(!audio_tempo_pull(&empty, &left, &right));
    CHECK_EQUAL(17, left);
    CHECK_EQUAL(19, right);
}

static void pitch_changes_follow_the_source_without_changing_speed(void)
{
    static const uint16_t SPEEDS[] = {75, 150, 200};
    for (uint32_t index = 0; index < 32000; index++) {
        double hz = index < 16000 ? 220 : 440;
        int32_t sample = (int32_t)lround(2000000 * sin(6.283185307179586 * hz * index / 16000));
        input[index][0] = sample;
        input[index][1] = -sample;
    }
    for (size_t index = 0; index < sizeof SPEEDS / sizeof SPEEDS[0]; index++) {
        uint32_t frames = render(16000, SPEEDS[index], 32000, 31);
        uint32_t boundary = 1600000u / SPEEDS[index];
        CHECK(fabs(frequency_between(16000, 1600, boundary - 1600) - 220) / 220 < 0.003);
        CHECK(fabs(frequency_between(16000, boundary + 1600, frames - 1600) - 440) / 440 < 0.003);
    }
}

static double spectral_power(uint32_t rate, uint32_t begin, uint32_t end, double hz)
{
    double coefficient = 2 * cos(6.283185307179586 * hz / rate);
    double q1 = 0, q2 = 0;
    for (uint32_t index = begin; index < end; index++) {
        double window = 0.5 - 0.5 * cos(6.283185307179586 * (index - begin) / (end - begin - 1));
        double q0 = output[index][0] * window + coefficient * q1 - q2;
        q2 = q1;
        q1 = q0;
    }
    return q1 * q1 + q2 * q2 - coefficient * q1 * q2;
}

static double spectral_peak(uint32_t rate, uint32_t frames, double hz)
{
    uint32_t begin = rate / 5;
    uint32_t end = frames - rate / 5;
    end = end < begin + rate ? end : begin + rate;
    double best = -1, peak = 0;
    for (int index = -24; index <= 24; index++) {
        double candidate = hz + (double)index / 4;
        double power = spectral_power(rate, begin, end, candidate);
        if (power > best) {
            best = power;
            peak = candidate;
        }
    }
    return peak;
}

static void mixed_music_tones_keep_both_spectral_peaks(void)
{
    static const uint16_t SPEEDS[] = {50, 150, 200};
    for (uint32_t index = 0; index < 32000; index++) {
        double phase = 6.283185307179586 * index / 16000;
        int32_t sample = (int32_t)lround(900000 * (sin(220 * phase) + sin(311 * phase)));
        input[index][0] = sample;
        input[index][1] = -sample;
    }
    for (size_t index = 0; index < sizeof SPEEDS / sizeof SPEEDS[0]; index++) {
        uint32_t frames = render(16000, SPEEDS[index], 32000, 317);
        CHECK(fabs(spectral_peak(16000, frames, 220) - 220) / 220 < 0.003);
        CHECK(fabs(spectral_peak(16000, frames, 311) - 311) / 311 < 0.003);
    }
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(unity_preserves_every_signed_24_bit_sample_and_stereo_channel),
        TEST_CASE(slower_and_faster_streams_have_the_requested_duration),
        TEST_CASE(changing_duration_preserves_pitch_and_stereo_phase),
        TEST_CASE(input_batches_and_output_backpressure_do_not_change_pcm),
        TEST_CASE(empty_and_subperiod_streams_end_at_the_rounded_duration),
        TEST_CASE(invalid_configuration_preserves_the_existing_stream),
        TEST_CASE(backpressure_and_invalid_pcm_do_not_consume_input),
        TEST_CASE(zero_tiny_and_adjacent_spans_preserve_wrapping_pcm),
        TEST_CASE(every_integer_speed_keeps_constant_pcm_and_exact_duration),
        TEST_CASE(an_unstarted_stream_can_be_drained_and_finished),
        TEST_CASE(pitch_changes_follow_the_source_without_changing_speed),
        TEST_CASE(mixed_music_tones_keep_both_spectral_peaks),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
