#include "device/audio_dsp.h"

#include "device/core1_source_private.h"

const AudioDspEntry AUDIO_DSP_MODULE_ENTRY = {
    .module = {.magic = AUDIO_DSP_MAGIC, .render = audio_resample_render},
    .aiff = aiff_probe,
    .probe = mp4_probe,
    .seek = mp4_cursor_seek,
    .next = mp4_cursor_next,
    .open_m4a = core1_open_m4a,
    .read_m4a = m4a_stream_read,
};
