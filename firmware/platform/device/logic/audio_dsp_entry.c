#include "device/audio_dsp.h"

#include "device/core1_source_private.h"

const AudioDspEntry AUDIO_DSP_MODULE_ENTRY = {
    .module = {.magic = AUDIO_DSP_MAGIC,
               .render = audio_resample_render,
               .open = {core1_open_m4a},
               .read = m4a_stream_read},
    .aiff = aiff_probe,
    .probe = mp4_probe,
    .seek = mp4_cursor_seek,
    .next = mp4_cursor_next,
};
