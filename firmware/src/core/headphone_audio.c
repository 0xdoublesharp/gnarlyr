#include "core/headphone_audio.h"

enum {
    GAIN_SHIFT = 16,
    OUTPUT_BITS = 24,
    SAMPLE_MAX = 8388607,
    SAMPLE_MIN = -8388608
};

HeadphoneAudio headphone_audio_defaults(void)
{
    return (HeadphoneAudio){.left = REPLAY_GAIN_UNITY,
                            .right = REPLAY_GAIN_UNITY,
                            .ceiling = HEADPHONE_AUDIO_VOLUME_MAX};
}

uint8_t headphone_audio_limit(HeadphoneAudio profile, uint8_t requested)
{
    return requested < profile.ceiling ? requested : profile.ceiling;
}

uint32_t headphone_audio_gain(uint32_t profile_gain, uint32_t input_gain)
{
    return (uint32_t)(((uint64_t)profile_gain * input_gain + REPLAY_GAIN_UNITY / 2) >> 16);
}

static int64_t floor_shift(int64_t value, int shift)
{
    return value >= 0 ? value >> shift : -((-value - 1) >> shift) - 1;
}

int32_t headphone_audio_sample(int32_t sample, uint8_t bits, uint32_t gain)
{
    if (bits == 0 || bits > 32) {
        return 0;
    }
    int64_t value = (int64_t)sample * gain;
    int shift = GAIN_SHIFT + bits - OUTPUT_BITS;
    if (shift > 0) {
        value = floor_shift(value + (INT64_C(1) << (shift - 1)), shift);
    } else if (shift < 0) {
        int64_t factor = INT64_C(1) << -shift;
        if (value > SAMPLE_MAX / factor || value < SAMPLE_MIN / factor) {
            return value < 0 ? SAMPLE_MIN : SAMPLE_MAX;
        }
        value *= factor;
    }
    return value > SAMPLE_MAX ? SAMPLE_MAX : value < SAMPLE_MIN ? SAMPLE_MIN : (int32_t)value;
}
