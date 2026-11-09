#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/audio.h"

enum {
    EQUALIZER_SECTIONS = HAL_AUDIO_EQUALIZER_BANDS,
    EQUALIZER_CHANNELS = 2,
    EQUALIZER_GAIN_MAX_HALF_DB = 20,
    EQUALIZER_HIGHEST_RATE = 96000,
    EQUALIZER_COEFFICIENT_BITS = 28
};

typedef struct EqualizerSection {
    int32_t b0;
    int32_t b1;
    int32_t b2;
    int32_t a1;
    int32_t a2;
} EqualizerSection;

typedef struct EqualizerHistory {
    int32_t x1;
    int32_t x2;
    int32_t y1;
    int32_t y2;
    uint32_t error;
} EqualizerHistory;

typedef struct Equalizer {
    EqualizerSection sections[EQUALIZER_SECTIONS];
    EqualizerHistory history[EQUALIZER_CHANNELS][EQUALIZER_SECTIONS];
    int section_count;
} Equalizer;

typedef struct EqualizerChange {
    const HalAudioEqualizer *before;
    const HalAudioEqualizer *after;
} EqualizerChange;

bool equalizer_design(Equalizer *equalizer, const HalAudioEqualizer *curve, uint32_t rate);
bool equalizer_retune(Equalizer *equalizer, EqualizerChange change, uint32_t rate);
void equalizer_run(Equalizer *equalizer, uint32_t *words, uint32_t frames);
