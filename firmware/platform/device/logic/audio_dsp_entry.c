#include "device/audio_dsp.h"

const AudioDspEntry AUDIO_DSP_MODULE_ENTRY = {
    .module = {.magic = AUDIO_DSP_MAGIC, .pull = audio_resample_pull},
    .probe = mp4_probe,
    .seek = mp4_cursor_seek,
    .next = mp4_cursor_next,
};
