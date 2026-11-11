#include "device/audio_vorbis.h"

#include "device/core1_source_private.h"

const AudioVorbisEntry AUDIO_VORBIS_MODULE_ENTRY = {
    .module = {.magic = AUDIO_VORBIS_MAGIC, .pull = audio_resample_pull},
    .open = core1_open_vorbis,
    .read = vorbis_stream_read,
};
