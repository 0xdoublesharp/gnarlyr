#pragma once

#include "device/audio_module.h"
#include "device/core1_stream.h"

#define AUDIO_APE_NUMBER 9u
#define AUDIO_APE_MAGIC 0x094D5344u

typedef struct AudioApeEntry {
    AudioModuleEntry module;
    bool (*open)(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);
    bool (*read)(void *context, uint32_t offset, void *buffer, uint32_t length);
} AudioApeEntry;
