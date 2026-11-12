#include "device/audio_effects_module.h"
#if defined(ECHO_DEBUG_LOOP) && defined(ECHO_DEVICE_MODULE)
#include "debug_runner.h"
#endif

const AudioEffectsEntry AUDIO_EFFECTS_MODULE_ENTRY = {
    .magic = AUDIO_EFFECTS_MAGIC,
    .fade_in = audio_fade_in,
    .fade_out = audio_fade_out,
    .effects_design = audio_effects_design,
    .effects_run = audio_effects_run,
    .effects_settle = audio_effects_settle,
#if defined(ECHO_DEBUG_LOOP) && defined(ECHO_DEVICE_MODULE)
    .debug = &DEBUG_CODE_ENTRY,
    .battery_trace = debug_battery_trace_write,
#endif
};
