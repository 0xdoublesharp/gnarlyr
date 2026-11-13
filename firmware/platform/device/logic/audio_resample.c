#include "device/audio_resample.h"
#include "device/sample_clamp.h"

#include <stddef.h>

#include "core/seam.h"

static int32_t filtered_input(const AudioResample *resample, int32_t sample)
{
    if (resample->factor == 1) {
        return sample;
    }
    return sample_clamp_24(sample);
}

uint32_t audio_resample_rate(uint32_t input_rate)
{
    static const uint32_t RATES[] = {8000,  11025, 12000, 16000, 22050,  24000, 32000,
                                     44100, 48000, 88200, 96000, 176400, 192000};
    static const uint8_t FACTORS[] = {4, 4, 4, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1};
    for (size_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
        if (RATES[index] == input_rate) {
            return input_rate * FACTORS[index];
        }
    }
    return 0;
}

CORE_COLD void audio_resample_start(AudioResample *resample, uint32_t input_rate)
{
    *resample = (AudioResample){0};
    uint32_t output_rate = audio_resample_rate(input_rate);
    resample->factor = input_rate != 0 ? (uint8_t)(output_rate / input_rate) : 0;
}

uint32_t audio_resample_room(const AudioResample *resample)
{
    if (resample->ended || resample->factor == 0) {
        return 0;
    }
    return AUDIO_RESAMPLE_HISTORY - AUDIO_RESAMPLE_LEFT -
           (uint32_t)(resample->frames - resample->cursor);
}

uint32_t audio_resample_push(AudioResample *resample, const int32_t *samples, uint32_t frames)
{
    uint32_t room = audio_resample_room(resample);
    frames = frames < room ? frames : room;
    for (uint32_t index = 0; index < frames; index++, samples += 2) {
        uint32_t at = (uint32_t)resample->frames & (AUDIO_RESAMPLE_HISTORY - 1);
        int32_t left = filtered_input(resample, samples[0]);
        int32_t right = filtered_input(resample, samples[1]);
        resample->history[at][0] = left;
        resample->history[at][1] = right;
        resample->history[at + AUDIO_RESAMPLE_HISTORY][0] = left;
        resample->history[at + AUDIO_RESAMPLE_HISTORY][1] = right;
        resample->stereo |= left != right;
        resample->frames++;
    }
    return frames;
}

void audio_resample_finish(AudioResample *resample)
{
    resample->ended = true;
}

uint32_t audio_resample_pending(const AudioResample *resample)
{
    return (uint32_t)((resample->frames - resample->cursor) * resample->factor) - resample->phase;
}
