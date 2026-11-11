#pragma once

#include "device/audio_dsp.h"
#include "device/audio_engine.h"

#define AUDIO_SOURCE_NUMBER 14u
#define AUDIO_SOURCE_MAGIC 0x0E4D5344u

typedef struct AudioSourceEntry {
    uint32_t magic;
    bool (*open_flac)(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);
} AudioSourceEntry;
