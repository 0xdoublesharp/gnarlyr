#pragma once

#include "device/asf_track.h"
#include "device/core1_stream.h"
#include "wmadec.h"

typedef struct Core1Wma {
    FlacInput input;
    StreamRegion region;
    WMADecodeContext *decoder;
    uint8_t *packet;
    uint32_t position;
    uint32_t packet_bytes;
    uint32_t next_sample;
    uint32_t generation;
    bool configured;
    uint8_t priming_frames;
    bool flushed;
    bool valid;
} Core1Wma;

Core1Decoder core1_wma_decoder(Core1Wma *wma, WMADecodeContext *decoder, uint8_t *packet,
                               StreamRegion region);
