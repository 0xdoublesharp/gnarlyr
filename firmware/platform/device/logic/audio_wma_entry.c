#include "device/audio_wma.h"

#include "device/asf_stream.h"
#include "device/core1_source_private.h"

const AudioWmaEntry AUDIO_WMA_MODULE_ENTRY = {
    .module = {.magic = AUDIO_WMA_MAGIC, .render = audio_resample_render},
    .open = core1_open_wma,
    .read = asf_stream_read,
};
