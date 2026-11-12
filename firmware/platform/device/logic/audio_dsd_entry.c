#include "device/audio_dsd.h"

const AudioDsdEntry AUDIO_DSD_MODULE_ENTRY = {
    .module = {.magic = AUDIO_DSD_MAGIC, .render = audio_resample_render},
    .open = core1_open_dsd,
    .read = dsd_stream_read,
};
