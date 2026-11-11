#include "device/audio_process.h"

const AudioProcessEntry AUDIO_PROCESS_MODULE_ENTRY = {
    .magic = AUDIO_PROCESS_MAGIC,
    .design = equalizer_design,
    .retune = equalizer_retune,
    .run = equalizer_run,
    .shape = audio_engine_stage_shape,
    .stretch = audio_engine_stage_stretch,
    .tempo_start = audio_tempo_stage_start,
    .tempo_finish = audio_tempo_stage_finish,
    .tempo_pending = audio_tempo_stage_pending,
};
