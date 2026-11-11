#pragma once

#include "device/core1_stream.h"
#include "device/ogg_record.h"
#include "device/opus_decode.h"

enum {
    OPUS_RATE = 48000,
    OPUS_RING_BYTES = 40960,
    OPUS_STATE_BYTES = 30720,
    OPUS_HEAD_BYTES = 19
};

typedef struct OpusWorkspace {
    uint8_t packet[OGG_PACKET_BYTES];
    _Alignas(8) uint8_t scratch[OPUS_DECODE_SCRATCH_BYTES];
    int16_t pcm[OPUS_DECODE_MAX_FRAMES * 2];
} OpusWorkspace;

typedef struct Core1Opus {
    OpusDecode decoder;
    FlacInput input;
    StreamRegion region;
    OpusWorkspace *work;
    uint32_t position;
    uint32_t generation;
    uint32_t origin;
    uint32_t pre_skip;
    uint32_t next;
    uint32_t staged;
    uint32_t taken;
    uint32_t first;
    uint32_t slot;
    bool origin_set;
    bool audio_started;
    bool anchored;
    _Alignas(8) uint8_t state[OPUS_STATE_BYTES];
} Core1Opus;

Core1Decoder core1_opus_decoder(Core1Opus *opus, StreamRegion region);
