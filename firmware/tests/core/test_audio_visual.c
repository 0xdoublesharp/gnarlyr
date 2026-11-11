#include <math.h>
#include <string.h>

#include "check.h"
#include "core/audio_visual.h"

static AudioVisualWork work;
static uint32_t words[AUDIO_VISUAL_FFT * 4];

static void fft_matches_a_double_dft_for_every_bin(void)
{
    for (int sample = 0; sample < AUDIO_VISUAL_FFT; sample++) {
        work.real[sample] = (int32_t)(12000 * sin(0.37 * sample) + 7000 * cos(0.91 * sample));
        work.imag[sample] = 0;
    }
    int32_t input[AUDIO_VISUAL_FFT];
    memcpy(input, work.real, sizeof input);
    audio_visual_fft(&work);
    for (int bin = 0; bin <= AUDIO_VISUAL_FFT / 2; bin++) {
        double real = 0;
        double imag = 0;
        for (int sample = 0; sample < AUDIO_VISUAL_FFT; sample++) {
            double angle = 6.283185307179586 * bin * sample / AUDIO_VISUAL_FFT;
            real += input[sample] * cos(angle) / AUDIO_VISUAL_FFT;
            imag -= input[sample] * sin(angle) / AUDIO_VISUAL_FFT;
        }
        CHECK(fabs(real - work.real[bin]) < 12);
        CHECK(fabs(imag - work.imag[bin]) < 12);
        double expected = sqrt(real * real + imag * imag);
        double measured = hypot(work.real[bin], work.imag[bin]);
        CHECK(fabs(expected - measured) < 14);
    }
}

static void ballistics_match_a_300_ms_reference_with_small_overshoot(void)
{
    AudioVisualNeedle needle = {0};
    double maximum = 0;
    for (int step = 1; step <= 100; step++) {
        audio_visual_needle(&needle, 20000, 8);
        double time = step * 0.008;
        double omega = 24;
        double damping = 0.82;
        double damped = omega * sqrt(1 - damping * damping);
        double expected =
            20000 * (1 - exp(-damping * omega * time) *
                             (cos(damped * time) + damping * omega / damped * sin(damped * time)));
        CHECK(fabs(needle.position / 256.0 - expected) < 1000);
        if (needle.position / 256.0 > maximum) {
            maximum = needle.position / 256.0;
        }
        if (step == 38) {
            CHECK(fabs(needle.position / 256.0 - 20000) < 600);
        }
    }
    CHECK(maximum > 20000 && maximum < 20600);
    audio_visual_needle(&needle, 0, 1000);
    CHECK(needle.position >= 0 && needle.position < 100);
}

static void logarithmic_bands_cover_each_positive_bin_once(void)
{
    int previous = 1;
    for (int band = 0; band < AUDIO_VISUAL_BANDS; band++) {
        CHECK_EQUAL(previous, audio_visual_band_edge(band));
        int next = audio_visual_band_edge(band + 1);
        CHECK(next > previous && next <= AUDIO_VISUAL_FFT / 2 + 1);
        previous = next;
    }
    CHECK_EQUAL(AUDIO_VISUAL_FFT / 2 + 1, previous);
}

static void ring_wrap_trimmed_channels_and_silence_are_measured(void)
{
    for (int frame = 0; frame < AUDIO_VISUAL_FFT * 2; frame++) {
        words[frame * 2] = (uint32_t)(int32_t)(16384 * sin(6.283185307179586 * frame * 12 / 256))
                           << 16;
        words[frame * 2 + 1] = 0;
    }
    AudioVisualBuffer buffer = {words, 512, 480, 512, 44100};
    AudioVisualLevels levels = audio_visual_measure(&work, buffer, true);
    CHECK(levels.mean[0] > 10000 && levels.mean[0] < 11000);
    CHECK_EQUAL(0, levels.mean[1]);
    CHECK(levels.peak[0] >= 16383);
    CHECK_EQUAL(0, levels.peak[1]);
    int largest = 0;
    for (int band = 1; band < AUDIO_VISUAL_BANDS; band++) {
        if (levels.band[band] > levels.band[largest]) {
            largest = band;
        }
    }
    CHECK(audio_visual_band_edge(largest) <= 24 && audio_visual_band_edge(largest + 1) > 24);
    memset(words, 0, sizeof words);
    levels = audio_visual_measure(&work, buffer, true);
    CHECK_EQUAL(0, levels.mean[0]);
    for (int band = 0; band < AUDIO_VISUAL_BANDS; band++) {
        CHECK_EQUAL(0, levels.band[band]);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(fft_matches_a_double_dft_for_every_bin),
        TEST_CASE(ballistics_match_a_300_ms_reference_with_small_overshoot),
        TEST_CASE(logarithmic_bands_cover_each_positive_bin_once),
        TEST_CASE(ring_wrap_trimmed_channels_and_silence_are_measured),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
