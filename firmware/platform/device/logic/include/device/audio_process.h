#pragma once

#include "device/audio_engine_stage.h"
#include "device/equalizer.h"

#define AUDIO_PROCESS_BASE 0x0308F000u
#define AUDIO_PROCESS_BYTES 0x3000u
#define AUDIO_PROCESS_NUMBER 7u
#define AUDIO_PROCESS_MAGIC 0x074D5344u

typedef struct AudioProcessEntry {
    uint32_t magic;
    bool (*design)(Equalizer *equalizer, const HalAudioEqualizer *curve, uint32_t rate);
    bool (*retune)(Equalizer *equalizer, EqualizerChange change, uint32_t rate);
    void (*run)(Equalizer *equalizer, uint32_t *words, uint32_t frames);
    void (*shape)(AudioEngine *engine, uint32_t first, bool tail);
    uint32_t (*stretch)(AudioEngine *engine, const int32_t *stage, uint32_t frames);
    bool (*tempo_start)(uint8_t *buffer, uint32_t frames, uint32_t rate, uint16_t speed);
    void (*tempo_finish)(void);
    uint32_t (*tempo_pending)(uint32_t input);
} AudioProcessEntry;
