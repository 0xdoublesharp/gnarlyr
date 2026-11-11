#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    AUDIO_RESAMPLE_HISTORY = 64,
    AUDIO_RESAMPLE_LEFT = 23,
    AUDIO_RESAMPLE_RIGHT = 24,
    AUDIO_RESAMPLE_FACTOR_LIMIT = 4
};

typedef struct AudioResample {
    int32_t history[2 * AUDIO_RESAMPLE_HISTORY][2];
    int32_t output[AUDIO_RESAMPLE_FACTOR_LIMIT][2];
    uint64_t frames;
    uint64_t cursor;
    uint8_t factor;
    uint8_t phase;
    bool ended;
    bool stereo;
} AudioResample;

uint32_t audio_resample_rate(uint32_t input_rate);
void audio_resample_start(AudioResample *resample, uint32_t input_rate);
uint32_t audio_resample_room(const AudioResample *resample);
uint32_t audio_resample_push(AudioResample *resample, const int32_t *samples, uint32_t frames);
uint32_t audio_resample_render(AudioResample *resample, uint32_t *words, uint32_t frames);
void audio_resample_finish(AudioResample *resample);
uint32_t audio_resample_pending(const AudioResample *resample);
