#include "device/equalizer.h"
#include "core/seam.h"
#include "device/sample_clamp.h"

enum {
    SAMPLE_SHIFT = 8
};

static const uint32_t ERROR_MASK = (UINT32_C(1) << EQUALIZER_COEFFICIENT_BITS) - 1;

static void run_section(const EqualizerSection *section, EqualizerHistory *history,
                        int32_t *samples, uint32_t frames)
{
    int32_t b0 = section->b0;
    int32_t b1 = section->b1;
    int32_t b2 = section->b2;
    int32_t a1 = section->a1;
    int32_t a2 = section->a2;
    int32_t x1 = history->x1;
    int32_t x2 = history->x2;
    int32_t y1 = history->y1;
    int32_t y2 = history->y2;
    uint32_t error = history->error;
    int32_t *sample = samples;
    int32_t *pair_end = samples + (frames & ~UINT32_C(1)) * EQUALIZER_CHANNELS;
    while (sample != pair_end) {
        int32_t x0 = sample[0];
        int64_t sum = (int64_t)error + (int64_t)b0 * x0 + (int64_t)b1 * x1 + (int64_t)b2 * x2 +
                      (int64_t)a1 * y1 + (int64_t)a2 * y2;
        int32_t y0 = sample_clamp_24((int32_t)(sum >> EQUALIZER_COEFFICIENT_BITS));
        error = (uint32_t)sum & ERROR_MASK;
        sample[0] = y0;
        x2 = x0;
        y2 = y0;

        int32_t x_next = sample[EQUALIZER_CHANNELS];
        sum = (int64_t)error + (int64_t)b0 * x_next + (int64_t)b1 * x2 + (int64_t)b2 * x1 +
              (int64_t)a1 * y2 + (int64_t)a2 * y1;
        int32_t y_next = sample_clamp_24((int32_t)(sum >> EQUALIZER_COEFFICIENT_BITS));
        error = (uint32_t)sum & ERROR_MASK;
        sample[EQUALIZER_CHANNELS] = y_next;
        x1 = x_next;
        y1 = y_next;
        sample += 2 * EQUALIZER_CHANNELS;
    }
    if ((frames & 1u) != 0) {
        int32_t x0 = sample[0];
        int64_t sum = (int64_t)error + (int64_t)b0 * x0 + (int64_t)b1 * x1 + (int64_t)b2 * x2 +
                      (int64_t)a1 * y1 + (int64_t)a2 * y2;
        int32_t y0 = sample_clamp_24((int32_t)(sum >> EQUALIZER_COEFFICIENT_BITS));
        error = (uint32_t)sum & ERROR_MASK;
        sample[0] = y0;
        x2 = x1;
        x1 = x0;
        y2 = y1;
        y1 = y0;
    }
    *history = (EqualizerHistory){x1, x2, y1, y2, error};
}

CORE_SEAM void equalizer_run(Equalizer *equalizer, uint32_t *words, uint32_t frames)
{
    int count = equalizer->section_count;
    if (count <= 0 || frames == 0) {
        return;
    }
    int32_t *samples = (int32_t *)words;
    uint32_t total = frames * EQUALIZER_CHANNELS;
    for (uint32_t index = 0; index < total; index++) {
        samples[index] = (int32_t)words[index] >> SAMPLE_SHIFT;
    }
    for (int section = 0; section < count; section++) {
        for (int channel = 0; channel < EQUALIZER_CHANNELS; channel++) {
            run_section(&equalizer->sections[section], &equalizer->history[channel][section],
                        samples + channel, frames);
        }
    }
    for (uint32_t index = 0; index < total; index++) {
        words[index] = (uint32_t)samples[index] << SAMPLE_SHIFT;
    }
}
