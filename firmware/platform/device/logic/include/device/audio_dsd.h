#pragma once

#include "device/audio_module.h"
#include "device/dsd_stream.h"

#define AUDIO_DSD_NUMBER 10u
#define AUDIO_DSD_MAGIC 0x0A4D5344u

typedef struct AudioDsdEntry {
    AudioModuleEntry module;
    bool (*open)(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);
    bool (*read)(void *context, uint32_t offset, void *buffer, uint32_t length);
} AudioDsdEntry;
