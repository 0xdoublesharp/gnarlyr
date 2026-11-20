#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/audio_engine.h"
#include "device/audio_resample.h"

#define AUDIO_MODULE_MAGIC(number) ((uint32_t)(number) << 24 | 0x4D5344u)

enum {
    AUDIO_MODULE_OPENS = 2
};

typedef struct AudioModuleEntry {
    uint32_t magic;
    uint32_t (*render)(AudioResample *resample, uint32_t *words, uint32_t frames);
    bool (*open[AUDIO_MODULE_OPENS])(void *context, const char *path, uint32_t start_ms,
                                     AudioTrackInfo *track);
    bool (*read)(void *context, uint32_t offset, void *buffer, uint32_t length);
} AudioModuleEntry;
