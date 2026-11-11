#include "device/audio_ape.h"

#include "device/ape_stream.h"
#include "device/core1_source_private.h"

const AudioApeEntry AUDIO_APE_MODULE_ENTRY = {
    .module = {.magic = AUDIO_APE_MAGIC, .render = audio_resample_render},
    .open = core1_open_ape,
    .read = ape_stream_read,
};
