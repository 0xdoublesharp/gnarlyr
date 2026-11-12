#pragma once

#include "device/audio_module.h"
#include "device/core1_stream.h"

#define AUDIO_APE_WMA_NUMBER 13u
#define AUDIO_APE_WMA_MAGIC 0x0D4D5344u

typedef struct AudioApeWmaEntry {
    AudioModuleEntry module;
    bool (*open_ape)(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);
    bool (*open_wma)(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);
    bool (*read_ape)(void *context, uint32_t offset, void *buffer, uint32_t length);
    bool (*read_wma)(void *context, uint32_t offset, void *buffer, uint32_t length);
} AudioApeWmaEntry;
