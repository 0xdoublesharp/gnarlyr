#pragma once

#include <stdint.h>

#include "device/core1_stream.h"
#include "device/mp3_decoder.h"
#include "device/mp3_probe.h"

typedef struct Core1Mp3 {
    Mp3Decoder *decoder;
    volatile const StreamControl *control;
    int32_t (*tail)[MP3_TAIL_CAPACITY];
    uint32_t next_sample;
    uint32_t tail_samples;
    uint32_t count;
    uint32_t head;
    uint32_t emitted;
} Core1Mp3;

Core1Decoder core1_mp3_decoder(Core1Mp3 *mp3, Mp3Decoder *decoder,
                               volatile const StreamControl *control,
                               int32_t (*tail)[MP3_TAIL_CAPACITY]);
