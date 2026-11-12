#include "device/audio_effects.h"

enum {
    SAMPLE_SHIFT = 8,
    GAIN_SHIFT = 16,
    GAIN_ROUNDING = 1 << (GAIN_SHIFT - 1),
    GATE_SCALE = AUDIO_BOOST_UNITY / AUDIO_BOOST_FLOOR,
    LAW_SHIFT = AUDIO_BOOST_TARGET_BITS + 2 * GAIN_SHIFT,
    SHORT_SHIFT = 5,
    MEDIUM_SHIFT = 6,
    LONG_SHIFT = 7,
    MEDIUM_RATE = 48000,
    LONG_RATE = 96000
};

void audio_boost_design(AudioBoost *boost, uint32_t rate)
{
    *boost = (AudioBoost){.gain = AUDIO_BOOST_UNITY,
                          .shift = rate > LONG_RATE     ? LONG_SHIFT
                                   : rate > MEDIUM_RATE ? MEDIUM_SHIFT
                                                        : SHORT_SHIFT};
}

static uint32_t square_root(uint64_t value)
{
    uint64_t root = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)root;
}

uint32_t audio_boost_target(uint32_t level)
{
    if (level <= AUDIO_BOOST_FLOOR) {
        return AUDIO_BOOST_UNITY;
    }
    uint32_t gate =
        level < AUDIO_BOOST_MAX_GAIN / GATE_SCALE ? level * GATE_SCALE : AUDIO_BOOST_MAX_GAIN;
    uint32_t law = square_root(((uint64_t)1 << LAW_SHIFT) / level);
    return law < gate ? law : gate;
}

static uint32_t magnitude(uint32_t word)
{
    int32_t sample = (int32_t)word >> SAMPLE_SHIFT;
    return (uint32_t)(sample < 0 ? -sample : sample);
}

static uint32_t chunk_peak(const uint32_t *words, uint32_t frames)
{
    uint32_t peak = 0;
    for (uint32_t index = 0; index < AUDIO_EFFECTS_CHANNELS * frames; index++) {
        uint32_t value = magnitude(words[index]);
        peak = value > peak ? value : peak;
    }
    return peak;
}

static uint32_t boosted(uint32_t word, uint32_t gain)
{
    int32_t sample = (int32_t)word >> SAMPLE_SHIFT;
    int64_t value = ((int64_t)sample * gain + GAIN_ROUNDING) >> GAIN_SHIFT;
    value = value > AUDIO_BOOST_CEILING    ? AUDIO_BOOST_CEILING
            : value < -AUDIO_BOOST_CEILING ? -AUDIO_BOOST_CEILING
                                           : value;
    return (uint32_t)(int32_t)value << SAMPLE_SHIFT;
}

static void boost_chunk(AudioBoost *boost, uint32_t *words, uint32_t frames)
{
    uint32_t peak = chunk_peak(words, frames);
    boost->level -=
        (uint32_t)(((uint64_t)boost->level * frames) >> (AUDIO_BOOST_RELEASE_SHIFT + boost->shift));
    boost->level = peak > boost->level ? peak : boost->level;
    uint32_t target = audio_boost_target(boost->level);
    uint32_t step = target > boost->gain ? (target - boost->gain) >> boost->shift : 0;
    boost->gain = target < boost->gain ? target : boost->gain;
    for (uint32_t index = 0; index < frames; index++, words += AUDIO_EFFECTS_CHANNELS) {
        boost->gain += step;
        words[0] = boosted(words[0], boost->gain);
        words[1] = boosted(words[1], boost->gain);
    }
}

void audio_boost_run(AudioBoost *boost, uint32_t *words, uint32_t frames)
{
    uint32_t chunk = 1u << boost->shift;
    while (frames > 0) {
        uint32_t count = frames < chunk ? frames : chunk;
        boost_chunk(boost, words, count);
        words += AUDIO_EFFECTS_CHANNELS * count;
        frames -= count;
    }
}
