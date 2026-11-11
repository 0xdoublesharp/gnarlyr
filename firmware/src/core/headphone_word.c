#include "core/headphone_audio.h"

enum {
    SAMPLE_SHIFT = 8,
    OUTPUT_BITS = 24
};

uint32_t headphone_audio_word(uint32_t word, uint32_t gain)
{
    if (gain == REPLAY_GAIN_UNITY) {
        return word;
    }
    int32_t sample = (int32_t)(word >> SAMPLE_SHIFT);
    if ((word & UINT32_C(0x80000000)) != 0) {
        sample -= 1 << OUTPUT_BITS;
    }
    return (uint32_t)headphone_audio_sample(sample, OUTPUT_BITS, gain) << SAMPLE_SHIFT;
}
