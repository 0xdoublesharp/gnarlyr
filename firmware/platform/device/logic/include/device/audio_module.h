#pragma once

#include "device/audio_resample.h"

typedef struct AudioModuleEntry {
    uint32_t magic;
    uint32_t (*render)(AudioResample *resample, uint32_t *words, uint32_t frames);
} AudioModuleEntry;
