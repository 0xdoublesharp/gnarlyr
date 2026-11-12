#pragma once

#include "demac.h"
#include "device/ape_track.h"
#include "device/core1_stream.h"

typedef struct Core1Ape {
    struct ape_ctx_t decoder;
    StreamRegion region;
    FlacInput input;
    uint8_t *buffer;
    uint32_t position;
    uint32_t remaining;
    uint32_t buffered;
    uint32_t consumed;
    uint32_t next_sample;
    uint32_t crc;
    int first_byte;
    bool configured;
    bool valid;
    bool frame_started;
    int32_t pcm[2][APE_CHUNK_SAMPLES];
} Core1Ape;

Core1Decoder core1_ape_decoder(Core1Ape *ape, uint8_t *buffer, StreamRegion region);
