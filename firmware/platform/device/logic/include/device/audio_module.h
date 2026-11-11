#pragma once

#include "device/audio_resample.h"

typedef struct AudioModuleEntry {
    uint32_t magic;
    bool (*pull)(AudioResample *resample, int32_t *left, int32_t *right);
} AudioModuleEntry;
