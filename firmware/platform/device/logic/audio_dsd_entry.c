#include "device/audio_dsd.h"

#include "device/core1_source_private.h"
#include "device/dsd_stream.h"

const AudioModuleEntry AUDIO_DSD_MODULE_ENTRY = {
    .magic = AUDIO_DSD_MAGIC,
    .render = audio_resample_render,
    .open = {core1_open_dsd},
    .read = {dsd_stream_read},
};
