#pragma once

#include "codec/flac.h"

enum {
    ASF_PACKET_BYTES = 16384,
    WMA_PACKET_BYTES = 4096,
    WMA_RING_BYTES = 8192,
    WMA_CONFIG_BYTES = 32,
    WMA_VIRTUAL_END = 0x7FFFFFFF
};

typedef struct AsfTrack {
    uint32_t data_offset;
    uint32_t data_end;
    uint32_t packet_bytes;
    uint32_t packets;
    uint32_t rate;
    uint32_t bitrate;
    uint32_t duration_ms;
    uint32_t samples;
    uint32_t preroll_ms;
    uint32_t frame_samples;
    uint16_t codec;
    uint16_t channels;
    uint16_t block_align;
    uint16_t extra_bytes;
    uint8_t stream;
    uint8_t extra[6];
} AsfTrack;

bool asf_track_probe(FlacInput input, AsfTrack *track);
