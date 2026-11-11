#include "device/audio_process.h"

const AudioProcessEntry AUDIO_PROCESS_MODULE_ENTRY = {
    .magic = AUDIO_PROCESS_MAGIC,
    .design = equalizer_design,
    .retune = equalizer_retune,
    .run = equalizer_run,
    .fade_in = audio_fade_in,
    .fade_out = audio_fade_out,
    .tempo_start = audio_tempo_start,
    .tempo_push = audio_tempo_push,
    .tempo_pull = audio_tempo_pull,
    .tempo_finish = audio_tempo_finish,
    .tempo_done = audio_tempo_done,
    .tempo_buffer_frames = audio_tempo_buffer_frames,
};
