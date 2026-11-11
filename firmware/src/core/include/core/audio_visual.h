#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    AUDIO_VISUAL_FFT = 256,
    AUDIO_VISUAL_BANDS = 16,
    AUDIO_VISUAL_FRAME_MS = 80
};

typedef struct AudioVisualBuffer {
    const uint32_t *words;
    uint32_t frames;
    uint32_t first;
    uint32_t count;
    uint32_t rate;
} AudioVisualBuffer;

typedef struct AudioVisualWork {
    int32_t real[AUDIO_VISUAL_FFT];
    int32_t imag[AUDIO_VISUAL_FFT];
} AudioVisualWork;

typedef struct AudioVisualLevels {
    uint16_t mean[2];
    uint16_t peak[2];
    uint8_t band[AUDIO_VISUAL_BANDS];
} AudioVisualLevels;

typedef struct AudioVisualNeedle {
    int32_t position;
    int32_t velocity;
} AudioVisualNeedle;

void audio_visual_fft(AudioVisualWork *work);
void audio_visual_needle(AudioVisualNeedle *needle, uint16_t target, uint32_t elapsed_ms);
int audio_visual_band_edge(int band);
uint8_t audio_visual_db(uint32_t magnitude);
AudioVisualLevels audio_visual_measure(AudioVisualWork *work, AudioVisualBuffer buffer,
                                       bool spectrum);
