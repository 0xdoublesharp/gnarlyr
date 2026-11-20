#include "device/audio_ape_wma.h"

#include "device/core1_source_private.h"

const AudioModuleEntry AUDIO_APE_WMA_MODULE_ENTRY = {
    .magic = AUDIO_APE_WMA_MAGIC,
    .render = audio_resample_render,
    .open = {core1_open_ape, core1_open_wma},
};
