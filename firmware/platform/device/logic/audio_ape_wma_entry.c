#include "device/audio_ape_wma.h"

#include "device/ape_stream.h"
#include "device/asf_stream.h"
#include "device/core1_source_private.h"

const AudioApeWmaEntry AUDIO_APE_WMA_MODULE_ENTRY = {
    .module = {.magic = AUDIO_APE_WMA_MAGIC, .render = audio_resample_render},
    .open_ape = core1_open_ape,
    .open_wma = core1_open_wma,
    .read_ape = ape_stream_read,
    .read_wma = asf_stream_read,
};
