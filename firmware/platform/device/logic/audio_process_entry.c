#include "device/audio_process.h"
#include "device/core1_source_private.h"
#if defined(ECHO_DEBUG_LOOP) && defined(ECHO_DEVICE_MODULE)
#include "debug_runner.h"
#endif

const AudioProcessEntry AUDIO_PROCESS_MODULE_ENTRY = {
    .magic = AUDIO_PROCESS_MAGIC,
    .design = equalizer_design,
    .retune = equalizer_retune,
    .run = equalizer_run,
    .fade_in = audio_fade_in,
    .fade_out = audio_fade_out,
    .open_flac = core1_open_flac,
#if defined(ECHO_DEBUG_LOOP) && defined(ECHO_DEVICE_MODULE)
    .debug = &DEBUG_CODE_ENTRY,
#endif
};
