#pragma once

#include "device/audio_fade.h"

#define AUDIO_EFFECTS_BASE 0x03092000u
#define AUDIO_EFFECTS_BYTES 0x2000u
#define AUDIO_EFFECTS_NUMBER 12u
#define AUDIO_EFFECTS_MAGIC 0x0C4D5344u

typedef struct AudioEffectsEntry {
    uint32_t magic;
    void (*fade_in)(AudioFadeBlock block, AudioFadeRamp *ramp);
    void (*fade_out)(AudioFadeBlock block, AudioFadeRamp *ramp, int64_t remaining);
#ifdef ECHO_DEBUG_LOOP
    const void *debug;
#endif
} AudioEffectsEntry;
