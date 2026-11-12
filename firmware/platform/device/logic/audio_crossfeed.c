#include "device/audio_effects.h"

enum {
    SAMPLE_SHIFT = 8,
    SERIES_TERMS = 8,
    OMEGA_SHIFT = 14,
    STATE_ROUNDING = 1 << (AUDIO_CROSSFEED_FRACTION_BITS - 1),
    OUTPUT_ROUNDING = 1 << (AUDIO_CROSSFEED_STATE_BITS - 1),
    SAMPLE_MAX = 8388607,
    SAMPLE_MIN = -8388608
};

typedef struct CrossfeedLevel {
    uint32_t cross_omega;
    uint32_t direct_omega;
    int32_t cross_gain;
    int32_t direct_cut;
} CrossfeedLevel;

static const CrossfeedLevel LEVELS[HAL_AUDIO_CROSSFEED_LEVELS - 1] = {
    {267653641, 368444788, 305538918, 161591248},
    {288242383, 401479045, 427464319, 220838376},
    {288242383, 409603915, 493628095, 245036994},
};

static const int64_t ONE = (int64_t)1 << AUDIO_CROSSFEED_FRACTION_BITS;

static int64_t product(int64_t first, int64_t second)
{
    return (first * second + STATE_ROUNDING) >> AUDIO_CROSSFEED_FRACTION_BITS;
}

static int64_t decay(uint32_t omega, uint32_t rate)
{
    int64_t angle = (int64_t)(((uint64_t)omega << OMEGA_SHIFT) / rate);
    int64_t term = ONE;
    int64_t sum = ONE;
    for (int index = 1; index <= SERIES_TERMS; index++) {
        term = -product(term, angle) / index;
        sum += term;
    }
    return sum;
}

void audio_crossfeed_design(AudioCrossfeed *crossfeed, uint8_t level, uint32_t rate)
{
    *crossfeed = (AudioCrossfeed){.level = level};
    if (level == 0 || level >= HAL_AUDIO_CROSSFEED_LEVELS || rate == 0) {
        return;
    }
    const CrossfeedLevel *chosen = &LEVELS[level - 1];
    int64_t low = decay(chosen->cross_omega, rate);
    int64_t high = decay(chosen->direct_omega, rate);
    int64_t cut = chosen->direct_cut;
    int64_t norm = ONE - cut + 2 * product(cut, high) + chosen->cross_gain;
    int64_t scale =
        (int64_t)(((uint64_t)1 << (2 * AUDIO_CROSSFEED_FRACTION_BITS)) / (uint64_t)norm);
    crossfeed->direct = (int32_t)product(ONE - product(cut, ONE - high), scale);
    crossfeed->direct_previous = (int32_t)product(-high, scale);
    crossfeed->direct_pole = (int32_t)high;
    crossfeed->cross = (int32_t)product(product(chosen->cross_gain, ONE - low), scale);
    crossfeed->cross_pole = (int32_t)low;
}

static int32_t clamp(int64_t sample)
{
    return sample > SAMPLE_MAX ? SAMPLE_MAX : sample < SAMPLE_MIN ? SAMPLE_MIN : (int32_t)sample;
}

static void feed(AudioCrossfeed *crossfeed, int channel, int32_t input)
{
    int64_t direct = (int64_t)crossfeed->direct * input +
                     (int64_t)crossfeed->direct_previous * crossfeed->input[channel] +
                     (int64_t)crossfeed->direct_pole * crossfeed->high[channel];
    int64_t cross = (int64_t)crossfeed->cross * input +
                    (int64_t)crossfeed->cross_pole * crossfeed->low[channel];
    crossfeed->high[channel] =
        (int32_t)((direct + STATE_ROUNDING) >> AUDIO_CROSSFEED_FRACTION_BITS);
    crossfeed->low[channel] = (int32_t)((cross + STATE_ROUNDING) >> AUDIO_CROSSFEED_FRACTION_BITS);
    crossfeed->input[channel] = input;
}

static uint32_t mixed(int32_t direct, int32_t cross)
{
    int64_t sum = ((int64_t)direct + cross + OUTPUT_ROUNDING) >> AUDIO_CROSSFEED_STATE_BITS;
    return (uint32_t)clamp(sum) << SAMPLE_SHIFT;
}

void audio_crossfeed_run(AudioCrossfeed *crossfeed, uint32_t *words, uint32_t frames)
{
    if (!crossfeed->running) {
        for (int channel = 0; channel < AUDIO_EFFECTS_CHANNELS; channel++) {
            crossfeed->input[channel] = 0;
            crossfeed->high[channel] = 0;
            crossfeed->low[channel] = 0;
        }
        crossfeed->running = true;
    }
    for (uint32_t index = 0; index < frames; index++, words += AUDIO_EFFECTS_CHANNELS) {
        feed(crossfeed, 0, ((int32_t)words[0] >> SAMPLE_SHIFT) * (1 << AUDIO_CROSSFEED_STATE_BITS));
        feed(crossfeed, 1, ((int32_t)words[1] >> SAMPLE_SHIFT) * (1 << AUDIO_CROSSFEED_STATE_BITS));
        words[0] = mixed(crossfeed->high[0], crossfeed->low[1]);
        words[1] = mixed(crossfeed->high[1], crossfeed->low[0]);
    }
}
