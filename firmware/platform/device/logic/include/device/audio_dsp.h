#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/audio_resample.h"
#include "device/mp4_demux.h"

#define AUDIO_DSP_BASE 0x03048000u
#define AUDIO_DSP_BYTES 0x2000u
#define AUDIO_DSP_NUMBER 5u
#define AUDIO_DSP_MAGIC 0x054D5344u

typedef struct AudioDspEntry {
    uint32_t magic;
    bool (*pull)(AudioResample *resample, int32_t *left, int32_t *right);
    bool (*probe)(FlacInput input, Mp4Track *track);
    bool (*seek)(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track, uint32_t sample);
    bool (*next)(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track, uint32_t *offset,
                 uint32_t *size);
} AudioDspEntry;
