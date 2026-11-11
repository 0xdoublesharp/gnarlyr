#pragma once

#include "device/audio_engine.h"
#include "device/audio_fade.h"
#include "device/equalizer.h"

#define AUDIO_PROCESS_BASE 0x0308F000u
#define AUDIO_PROCESS_BYTES 0x5000u
#define AUDIO_PROCESS_NUMBER 7u
#define AUDIO_PROCESS_MAGIC 0x074D5344u

typedef struct AudioProcessEntry {
    uint32_t magic;
    bool (*design)(Equalizer *equalizer, const HalAudioEqualizer *curve, uint32_t rate);
    bool (*retune)(Equalizer *equalizer, EqualizerChange change, uint32_t rate);
    void (*run)(Equalizer *equalizer, uint32_t *words, uint32_t frames);
    void (*fade_in)(AudioFadeBlock block, AudioFadeRamp *ramp);
    void (*fade_out)(AudioFadeBlock block, AudioFadeRamp *ramp, int64_t remaining);
    bool (*open_flac)(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);
#ifdef ECHO_DEBUG_LOOP
    const void *debug;
#endif
} AudioProcessEntry;
