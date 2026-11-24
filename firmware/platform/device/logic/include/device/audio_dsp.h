#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/audio_module.h"
#include "device/wav_source.h"

#define AUDIO_DSP_BASE 0x03048000u
#define AUDIO_DSP_BYTES 0x2000u
#define AUDIO_DSP_NUMBER 5u
#define AUDIO_DSP_MAGIC AUDIO_MODULE_MAGIC(AUDIO_DSP_NUMBER)

typedef struct AudioDspEntry {
    AudioModuleEntry module;
    bool (*aiff)(FlacInput input, WavFormat *format);
} AudioDspEntry;
