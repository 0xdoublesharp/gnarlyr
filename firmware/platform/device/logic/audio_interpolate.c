#include "device/audio_resample.h"

#include "audio_resample_table.h"

enum {
    FRACTION_BITS = 30,
    SAMPLE_MAX = 8388607,
    SAMPLE_MIN = -8388608,
    HISTORY_MASK = AUDIO_RESAMPLE_HISTORY - 1,
    TAPS = AUDIO_RESAMPLE_LEFT + AUDIO_RESAMPLE_RIGHT + 1,
    HALF_TAPS = TAPS / 2,
    QUARTERS = 4,
    SAMPLE_SHIFT = 8
};

typedef struct Fold {
    int32_t (*first)[2];
    int32_t (*last)[2];
    const int32_t *coefficients;
    int count;
} Fold;

typedef struct Totals {
    int64_t sample[2];
} Totals;

static int32_t round_sample(int64_t value, int shift)
{
    value += INT64_C(1) << (shift - 1);
    int64_t rounded = value >= 0 ? value >> shift : -((-value - 1) >> shift) - 1;
    return rounded > SAMPLE_MAX ? SAMPLE_MAX : rounded < SAMPLE_MIN ? SAMPLE_MIN : (int32_t)rounded;
}

__attribute__((noinline)) static Totals sum_mono(const Fold *fold)
{
    const int32_t *first = fold->first[0];
    const int32_t *last = fold->last[0];
    const int32_t *coefficient = fold->coefficients;
    const int32_t *end = coefficient + fold->count;
    int64_t total = 0;
    do {
        total += (int64_t)*coefficient++ * (*first + *last);
        first += 2;
        last -= 2;
    } while (coefficient != end);
    return (Totals){{total, total}};
}

__attribute__((noinline)) static Totals difference_mono(const Fold *fold)
{
    const int32_t *first = fold->first[0];
    const int32_t *last = fold->last[0];
    const int32_t *coefficient = fold->coefficients;
    const int32_t *end = coefficient + fold->count;
    int64_t total = 0;
    do {
        total += (int64_t)*coefficient++ * (*first - *last);
        first += 2;
        last -= 2;
    } while (coefficient != end);
    return (Totals){{total, total}};
}

__attribute__((noinline)) static Totals sum_stereo(const Fold *fold)
{
    const int32_t *first = fold->first[0];
    const int32_t *last = fold->last[0];
    const int32_t *coefficient = fold->coefficients;
    const int32_t *end = coefficient + fold->count;
    int64_t left = 0;
    int64_t right = 0;
    do {
        int64_t value = *coefficient++;
        left += value * (first[0] + last[0]);
        right += value * (first[1] + last[1]);
        first += 2;
        last -= 2;
    } while (coefficient != end);
    return (Totals){{left, right}};
}

__attribute__((noinline)) static Totals difference_stereo(const Fold *fold)
{
    const int32_t *first = fold->first[0];
    const int32_t *last = fold->last[0];
    const int32_t *coefficient = fold->coefficients;
    const int32_t *end = coefficient + fold->count;
    int64_t left = 0;
    int64_t right = 0;
    do {
        int64_t value = *coefficient++;
        left += value * (first[0] - last[0]);
        right += value * (first[1] - last[1]);
        first += 2;
        last -= 2;
    } while (coefficient != end);
    return (Totals){{left, right}};
}

typedef struct Kernel {
    int32_t (*window)[2];
    Totals (*sum)(const Fold *fold);
    Totals (*difference)(const Fold *fold);
    int32_t (*output)[2];
} Kernel;

static void centre(const Kernel *kernel)
{
    int32_t (*window)[2] = kernel->window;
    Totals totals =
        kernel->sum(&(Fold){window, window + TAPS - 2, RESAMPLE_CENTRE, AUDIO_RESAMPLE_LEFT});
    for (int channel = 0; channel < 2; channel++) {
        int64_t total =
            totals.sample[channel] +
            (int64_t)RESAMPLE_CENTRE[AUDIO_RESAMPLE_LEFT] * window[AUDIO_RESAMPLE_LEFT][channel] +
            (int64_t)RESAMPLE_CENTRE[AUDIO_RESAMPLE_LEFT + 1] * window[TAPS - 1][channel];
        kernel->output[0][channel] = round_sample(total, FRACTION_BITS);
    }
}

static void half(const Kernel *kernel, uint32_t phase)
{
    int32_t (*window)[2] = kernel->window;
    Totals totals = kernel->sum(&(Fold){window, window + TAPS - 1, RESAMPLE_HALF, HALF_TAPS});
    for (int channel = 0; channel < 2; channel++) {
        kernel->output[phase][channel] = round_sample(totals.sample[channel], FRACTION_BITS);
    }
}

static void quarters(const Kernel *kernel)
{
    int32_t (*window)[2] = kernel->window;
    Totals sums = kernel->sum(&(Fold){window, window + TAPS - 1, RESAMPLE_QUARTER_SUM, HALF_TAPS});
    Totals differences = kernel->difference(
        &(Fold){window, window + TAPS - 1, RESAMPLE_QUARTER_DIFFERENCE, HALF_TAPS});
    for (int channel = 0; channel < 2; channel++) {
        int64_t sum = sums.sample[channel];
        int64_t difference = differences.sample[channel];
        kernel->output[1][channel] = round_sample(sum + difference, FRACTION_BITS + 1);
        kernel->output[3][channel] = round_sample(sum - difference, FRACTION_BITS + 1);
    }
}

static int32_t (*gathered(AudioResample *resample, int32_t (*window)[2]))[2]
{
    uint32_t start = ((uint32_t)resample->cursor - AUDIO_RESAMPLE_LEFT) & HISTORY_MASK;
    uint64_t available = resample->frames - resample->cursor;
    if (available > AUDIO_RESAMPLE_RIGHT) {
        return resample->history + start;
    }
    for (uint32_t tap = 0; tap < TAPS; tap++) {
        bool present = tap < AUDIO_RESAMPLE_LEFT + available;
        window[tap][0] = present ? resample->history[start + tap][0] : 0;
        window[tap][1] = present ? resample->history[start + tap][1] : 0;
    }
    return window;
}

static void interpolate(AudioResample *resample)
{
    int32_t window[TAPS][2];
    uint32_t factor = resample->factor;
    Kernel kernel = {gathered(resample, window), resample->stereo ? sum_stereo : sum_mono,
                     resample->stereo ? difference_stereo : difference_mono, resample->output};
    centre(&kernel);
    if (factor == QUARTERS) {
        quarters(&kernel);
    }
    half(&kernel, factor / 2);
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
