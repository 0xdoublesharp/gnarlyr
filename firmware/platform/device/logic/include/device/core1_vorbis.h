#pragma once

#include "device/core1_stream.h"
#include "device/vorbis_decoder.h"
#include "device/vorbis_record.h"

_Static_assert((uint32_t)VORBIS_MAX_FRAMES <= (uint32_t)FLAC_MAX_BLOCK,
               "Vorbis output must fit a shared audio slot");

typedef struct Core1Vorbis {
    VorbisDecoder decoder;
    FlacInput input;
    StreamRegion region;
    uint32_t position;
    uint32_t next_sample;
    uint32_t slot;
    uint32_t generation;
    bool anchored;
    bool valid;
    uint8_t packet[VORBIS_PACKET_BYTES];
} Core1Vorbis;

Core1Decoder core1_vorbis_decoder(Core1Vorbis *vorbis, StreamRegion region);
