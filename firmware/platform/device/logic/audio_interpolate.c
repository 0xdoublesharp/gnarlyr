#include "device/audio_resample.h"
#include "device/sample_clamp.h"

#include "audio_resample_table.h"

enum {
    FRACTION_BITS = 30,
    HISTORY_MASK = AUDIO_RESAMPLE_HISTORY - 1,
    TAPS = AUDIO_RESAMPLE_LEFT + AUDIO_RESAMPLE_RIGHT + 1,
    HALF_TAPS = TAPS / 2,
    QUARTERS = 4,
    SAMPLE_SHIFT = 8
};

typedef struct Totals {
    int64_t sample[2];
} Totals;

static inline int32_t round_sample(int64_t value, int shift)
{
    return sample_clamp_24((int32_t)((value + (INT64_C(1) << (shift - 1))) >> shift));
}

__attribute__((noinline)) static int64_t fold_mono(const int32_t *first, const int32_t *last,
                                                   const int32_t *coefficients, uint32_t count)
{
    int64_t total = 0;
    do {
        int32_t a = *first;
        int32_t b = *last;
        first += 2;
        last -= 2;
        total += (int64_t)*coefficients++ * (a + b);
    } while (--count != 0);
    return total;
}

__attribute__((noinline)) static Totals fold_stereo(const int32_t *first, const int32_t *last,
                                                    const int32_t *coefficients, uint32_t count)
{
    int64_t left = 0;
    int64_t right = 0;
    do {
        int64_t coefficient = *coefficients++;
        left += coefficient * (first[0] + last[0]);
        right += coefficient * (first[1] + last[1]);
        first += 2;
        last -= 2;
    } while (--count != 0);
    return (Totals){{left, right}};
}

__attribute__((noinline)) static void quarters_and_half_mono(int32_t (*window)[2],
                                                             int32_t (*output)[2])
{
    const int32_t *first = window[0];
    const int32_t *last = window[TAPS - 1];
    const int32_t *half_coeff = RESAMPLE_HALF;
    const int32_t *sum_coeff = RESAMPLE_QUARTER_SUM;
    const int32_t *diff_coeff = RESAMPLE_QUARTER_DIFFERENCE;
    int64_t half_total = 0;
    int64_t sum_total = 0;
    int64_t diff_total = 0;
    for (uint32_t index = 0; index < HALF_TAPS; index++) {
        int32_t a = *first;
        int32_t b = *last;
        first += 2;
        last -= 2;
        int32_t s = a + b;
        int32_t d = a - b;
        half_total += (int64_t)half_coeff[index] * s;
        sum_total += (int64_t)sum_coeff[index] * s;
        diff_total += (int64_t)diff_coeff[index] * d;
    }
    int32_t q1 = round_sample(sum_total + diff_total, FRACTION_BITS + 1);
    int32_t q2 = round_sample(half_total, FRACTION_BITS);
    int32_t q3 = round_sample(sum_total - diff_total, FRACTION_BITS + 1);
    output[1][0] = q1;
    output[1][1] = q1;
    output[2][0] = q2;
    output[2][1] = q2;
    output[3][0] = q3;
    output[3][1] = q3;
}

__attribute__((noinline)) static void quarters_stereo(int32_t (*window)[2], int32_t (*output)[2])
{
    const int32_t *first = window[0];
    const int32_t *last = window[TAPS - 1];
    int64_t sum_l = 0;
    int64_t sum_r = 0;
    int64_t diff_l = 0;
    int64_t diff_r = 0;
    for (uint32_t index = 0; index < HALF_TAPS; index++) {
        int32_t fl = first[0];
        int32_t fr = first[1];
        int32_t ll = last[0];
        int32_t lr = last[1];
        first += 2;
        last -= 2;
        int64_t cs = RESAMPLE_QUARTER_SUM[index];
        int64_t cd = RESAMPLE_QUARTER_DIFFERENCE[index];
        sum_l += cs * (fl + ll);
        sum_r += cs * (fr + lr);
        diff_l += cd * (fl - ll);
        diff_r += cd * (fr - lr);
    }
    output[1][0] = round_sample(sum_l + diff_l, FRACTION_BITS + 1);
    output[1][1] = round_sample(sum_r + diff_r, FRACTION_BITS + 1);
    output[3][0] = round_sample(sum_l - diff_l, FRACTION_BITS + 1);
    output[3][1] = round_sample(sum_r - diff_r, FRACTION_BITS + 1);
}

static void interpolate_window(int32_t (*window)[2], uint32_t factor, bool stereo,
                               int32_t (*output)[2])
{
    if (!stereo) {
        int64_t c = fold_mono(window[0], window[TAPS - 2], RESAMPLE_CENTRE, AUDIO_RESAMPLE_LEFT);
        c += (int64_t)RESAMPLE_CENTRE[AUDIO_RESAMPLE_LEFT] * window[AUDIO_RESAMPLE_LEFT][0] +
             (int64_t)RESAMPLE_CENTRE[AUDIO_RESAMPLE_LEFT + 1] * window[TAPS - 1][0];
        int32_t c0 = round_sample(c, FRACTION_BITS);
        output[0][0] = c0;
        output[0][1] = c0;
        if (factor == QUARTERS) {
            quarters_and_half_mono(window, output);
            return;
        }
        int32_t h0 = round_sample(fold_mono(window[0], window[TAPS - 1], RESAMPLE_HALF, HALF_TAPS),
                                  FRACTION_BITS);
        output[1][0] = h0;
        output[1][1] = h0;
        return;
    }
    Totals centre = fold_stereo(window[0], window[TAPS - 2], RESAMPLE_CENTRE, AUDIO_RESAMPLE_LEFT);
    for (int channel = 0; channel < 2; channel++) {
        int64_t total =
            centre.sample[channel] +
            (int64_t)RESAMPLE_CENTRE[AUDIO_RESAMPLE_LEFT] * window[AUDIO_RESAMPLE_LEFT][channel] +
            (int64_t)RESAMPLE_CENTRE[AUDIO_RESAMPLE_LEFT + 1] * window[TAPS - 1][channel];
        output[0][channel] = round_sample(total, FRACTION_BITS);
    }
    if (factor == QUARTERS) {
        quarters_stereo(window, output);
    }
    Totals half = fold_stereo(window[0], window[TAPS - 1], RESAMPLE_HALF, HALF_TAPS);
    output[factor / 2][0] = round_sample(half.sample[0], FRACTION_BITS);
    output[factor / 2][1] = round_sample(half.sample[1], FRACTION_BITS);
}

__attribute__((noinline)) static void interpolate_tail(AudioResample *resample, uint32_t start,
                                                       uint64_t available)
{
    int32_t window[TAPS][2];
    for (uint32_t tap = 0; tap < TAPS; tap++) {
        bool present = tap < AUDIO_RESAMPLE_LEFT + available;
        window[tap][0] = present ? resample->history[start + tap][0] : 0;
        window[tap][1] = present ? resample->history[start + tap][1] : 0;
    }
    interpolate_window(window, resample->factor, resample->stereo, resample->output);
}

static void interpolate(AudioResample *resample)
{
    uint32_t start = ((uint32_t)resample->cursor - AUDIO_RESAMPLE_LEFT) & HISTORY_MASK;
    uint64_t available = resample->frames - resample->cursor;
    if (available > AUDIO_RESAMPLE_RIGHT) {
        interpolate_window(resample->history + start, resample->factor, resample->stereo,
                           resample->output);
        return;
    }
    interpolate_tail(resample, start, available);
}

static bool ready(const AudioResample *resample)
{
    return resample->factor != 0 && resample->cursor != resample->frames &&
           (resample->ended || resample->factor == 1 ||
            resample->frames - resample->cursor > AUDIO_RESAMPLE_RIGHT);
}

uint32_t audio_resample_render(AudioResample *resample, uint32_t *words, uint32_t frames)
{
    uint32_t *start = words;
    uint32_t *end = words + 2 * frames;
    while (words != end && ready(resample)) {
        int32_t (*phases)[2] = &resample->history[(uint32_t)resample->cursor & HISTORY_MASK];
        if (resample->factor > 1) {
            if (resample->phase == 0) {
                interpolate(resample);
            }
            phases = resample->output;
        }
        uint32_t phase = resample->phase;
        uint32_t last = resample->factor;
        for (; phase < last && words != end; phase++, words += 2) {
            words[0] = (uint32_t)phases[phase][0] << SAMPLE_SHIFT;
            words[1] = (uint32_t)phases[phase][1] << SAMPLE_SHIFT;
        }
        resample->phase = (uint8_t)(phase == last ? 0 : phase);
        resample->cursor += phase == last;
    }
    return (uint32_t)(words - start) / 2;
}
