#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    AUDIO_FADE_UNITY = 32768,
    AUDIO_FADE_SHIFT = 15
};

typedef struct AudioFadeSpan {
    int32_t first;
    int32_t step;
    uint32_t length;
    uint32_t reciprocal;
} AudioFadeSpan;

typedef struct AudioFadeRamp {
    uint32_t start;
    uint32_t length;
    uint32_t reciprocal;
    bool active;
    bool begun;
} AudioFadeRamp;

typedef struct AudioFadeBlock {
    uint32_t *ring;
    uint32_t ring_frames;
    uint32_t first;
    uint32_t frames;
} AudioFadeBlock;

void audio_fade_run(uint32_t *words, uint32_t frames, AudioFadeSpan span);
void audio_fade_in(AudioFadeBlock block, AudioFadeRamp *ramp);
void audio_fade_out(AudioFadeBlock block, AudioFadeRamp *ramp, int64_t remaining);
