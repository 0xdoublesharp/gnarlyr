#include "device/audio_effects_module.h"
#if defined(ECHO_DEBUG_LOOP) && defined(ECHO_DEVICE_MODULE)
#include "debug_runner.h"
#endif

const AudioEffectsEntry AUDIO_EFFECTS_MODULE_ENTRY = {
    .magic = AUDIO_EFFECTS_MAGIC,
    .fade_in = audio_fade_in,
    .fade_out = audio_fade_out,
#if defined(ECHO_DEBUG_LOOP) && defined(ECHO_DEVICE_MODULE)
    .debug = &DEBUG_CODE_ENTRY,
#endif
};
