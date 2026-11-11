#include "device/audio_ogg.h"

#include "device/core1_source_private.h"

const AudioOggEntry AUDIO_OGG_MODULE_ENTRY = {
    .module = {.magic = AUDIO_OGG_MAGIC, .render = audio_resample_render},
    .open = core1_open_ogg,
    .read = ogg_stream_read,
};
