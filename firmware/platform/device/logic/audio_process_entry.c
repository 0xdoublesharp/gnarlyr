#include "device/audio_process.h"

const AudioProcessEntry AUDIO_PROCESS_MODULE_ENTRY = {
    .magic = AUDIO_PROCESS_MAGIC,
    .design = equalizer_design,
    .retune = equalizer_retune,
    .run = equalizer_run,
    .set_equalizer = audio_engine_set_equalizer,
    .convert = audio_convert_pending,
    .shape = audio_shape_block,
    .sleep = audio_engine_set_sleep_remaining,
    .repeat_mark = audio_engine_repeat_mark,
};
