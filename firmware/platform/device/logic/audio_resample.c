#include "device/audio_resample.h"

#include <stddef.h>

uint32_t audio_resample_rate(uint32_t input_rate)
{
    static const uint32_t RATES[] = {8000,  11025, 12000, 16000, 22050,  24000, 32000,
                                     44100, 48000, 88200, 96000, 176400, 192000};
    static const uint8_t FACTORS[] = {6, 4, 4, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1};
    for (size_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
        if (RATES[index] == input_rate) {
            return input_rate * FACTORS[index];
        }
    }
    return 0;
}

void audio_resample_start(AudioResample *resample, uint32_t input_rate)
{
    *resample = (AudioResample){0};
    uint32_t output_rate = audio_resample_rate(input_rate);
    resample->factor = input_rate != 0 ? (uint8_t)(output_rate / input_rate) : 0;
}

bool audio_resample_push(AudioResample *resample, int32_t left, int32_t right)
{
    if (resample->ended || resample->factor == 0 ||
        resample->frames - resample->cursor >= AUDIO_RESAMPLE_HISTORY - AUDIO_RESAMPLE_LEFT) {
        return false;
    }
    uint32_t at = (uint32_t)resample->frames & (AUDIO_RESAMPLE_HISTORY - 1);
    resample->history[0][at] = left;
    resample->history[1][at] = right;
    resample->frames++;
    return true;
}

void audio_resample_finish(AudioResample *resample)
{
    resample->ended = true;
}

uint32_t audio_resample_pending(const AudioResample *resample)
{
    return (uint32_t)((resample->frames - resample->cursor) * resample->factor) - resample->phase;
}
