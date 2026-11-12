#include "device/audio_source.h"

#include "device/core1_source_private.h"

const AudioSourceEntry AUDIO_SOURCE_MODULE_ENTRY = {
    .magic = AUDIO_SOURCE_MAGIC,
    .open_flac = core1_open_flac,
};
