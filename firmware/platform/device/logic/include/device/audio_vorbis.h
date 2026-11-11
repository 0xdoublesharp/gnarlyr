#pragma once

#include "device/audio_module.h"
#include "device/core1_stream.h"

#define AUDIO_VORBIS_NUMBER 6u
#define AUDIO_VORBIS_MAGIC 0x064D5344u

typedef struct AudioVorbisEntry {
    AudioModuleEntry module;
    bool (*open)(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);
    bool (*read)(void *context, uint32_t offset, void *buffer, uint32_t length);
} AudioVorbisEntry;
