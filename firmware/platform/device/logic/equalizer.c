#include "device/equalizer.h"
#include "core/seam.h"
#include "device/sample_clamp.h"

enum {
    SAMPLE_SHIFT = 8
};

static const uint32_t ERROR_MASK = (UINT32_C(1) << EQUALIZER_COEFFICIENT_BITS) - 1;

static int32_t clamped(int32_t value)
{
    return sample_clamp_24(value);
}

static void run_section(EqualizerSection k, EqualizerHistory *history, uint32_t *words,
                        uint32_t frames)
{
    EqualizerHistory h = *history;
    for (uint32_t index = 0; index < frames; index++) {
        uint32_t *word = &words[index * EQUALIZER_CHANNELS];
        int32_t x = (int32_t)*word >> SAMPLE_SHIFT;
        int64_t sum = (int64_t)h.error + (int64_t)k.b0 * x + (int64_t)k.b1 * h.x1 +
                      (int64_t)k.b2 * h.x2 + (int64_t)k.a1 * h.y1 + (int64_t)k.a2 * h.y2;
        int32_t y = clamped((int32_t)(sum >> EQUALIZER_COEFFICIENT_BITS));
        h.error = (uint32_t)sum & ERROR_MASK;
        h.x2 = h.x1;
        h.x1 = x;
        h.y2 = h.y1;
        h.y1 = y;
        *word = (uint32_t)y << SAMPLE_SHIFT;
    }
    *history = h;
}

CORE_SEAM void equalizer_run(Equalizer *equalizer, uint32_t *words, uint32_t frames)
{
    for (int section = 0; section < equalizer->section_count; section++) {
        for (int channel = 0; channel < EQUALIZER_CHANNELS; channel++) {
            run_section(equalizer->sections[section], &equalizer->history[channel][section],
                        words + channel, frames);
        }
    }
}
