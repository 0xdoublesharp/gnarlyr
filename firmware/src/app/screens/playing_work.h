#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/audio_visual.h"
#include "gfx/canvas.h"
#include "settings/settings.h"

enum {
    PLAYING_SHUFFLE_SIZE = 48,
    PLAYING_NEON_PIXELS = 1024
};

typedef enum PlayingWorkOwner {
    PLAYING_WORK_NONE,
    PLAYING_WORK_SHUFFLE,
    PLAYING_WORK_STYLES
} PlayingWorkOwner;

typedef struct MeterState {
    AudioVisualNeedle needles[2];
    uint8_t shown[2];
    uint8_t echo[2];
    uint32_t peak_until[2];
    uint8_t bars[AUDIO_VISUAL_BANDS];
    uint8_t caps[AUDIO_VISUAL_BANDS];
    uint32_t cap_until[AUDIO_VISUAL_BANDS];
    uint32_t frame;
    uint32_t updated_ms;
    uint32_t text_changed_ms;
    uint16_t track;
    NowPlayingStyle style;
    bool active;
} MeterState;

typedef struct PlayingStyleWork {
    MeterState meter;
    union {
        AudioVisualWork audio;
        Color pixels[PLAYING_NEON_PIXELS];
    } neon;
} PlayingStyleWork;

typedef struct PlayingModuleWork {
    uint8_t owner;
    union {
        Color shuffle[PLAYING_SHUFFLE_SIZE * PLAYING_SHUFFLE_SIZE];
        PlayingStyleWork styles;
    } as;
} PlayingModuleWork;

extern PlayingModuleWork playing_module_work;

bool playing_work_claim_shuffle(void);
PlayingStyleWork *playing_work_styles(void);
