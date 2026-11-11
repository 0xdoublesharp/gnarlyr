#pragma once

#include "device/audio_engine.h"
#include "device/audio_fade.h"
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
    void (*fade_in)(AudioFadeBlock block, AudioFadeRamp *ramp);
    void (*fade_out)(AudioFadeBlock block, AudioFadeRamp *ramp, int64_t remaining);
#ifdef ECHO_DEBUG_LOOP
    const void *debug;
#endif
    void (*set_equalizer)(AudioEngine *engine, const HalAudioEqualizer *curve);
    void (*convert)(const AudioEngine *engine, uint32_t *words, uint32_t frames, int shift);
    void (*shape)(AudioEngine *engine, uint32_t first, bool tail);
    void (*sleep)(AudioEngine *engine, uint32_t remaining_ms);
    uint8_t (*repeat_mark)(AudioEngine *engine, bool clear);
} AudioProcessEntry;
