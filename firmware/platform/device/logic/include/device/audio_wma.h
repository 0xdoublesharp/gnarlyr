#pragma once

#include "device/audio_module.h"
#include "device/core1_stream.h"

#define AUDIO_WMA_NUMBER 10u
#define AUDIO_WMA_MAGIC 0x0A4D5344u

typedef struct AudioWmaEntry {
    AudioModuleEntry module;
    bool (*open)(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);
    bool (*read)(void *context, uint32_t offset, void *buffer, uint32_t length);
} AudioWmaEntry;
