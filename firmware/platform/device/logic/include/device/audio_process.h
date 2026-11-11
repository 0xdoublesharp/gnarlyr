#pragma once

#include "device/audio_fade.h"
#include "device/audio_tempo.h"
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
    bool (*tempo_start)(AudioTempo *tempo, AudioTempoConfig config);
    bool (*tempo_push)(AudioTempo *tempo, int32_t left, int32_t right);
    bool (*tempo_pull)(AudioTempo *tempo, int32_t *left, int32_t *right);
    void (*tempo_finish)(AudioTempo *tempo);
    bool (*tempo_done)(const AudioTempo *tempo);
    uint32_t (*tempo_buffer_frames)(uint32_t rate);
} AudioProcessEntry;
