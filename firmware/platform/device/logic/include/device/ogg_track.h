#pragma once

#include "codec/ogg_packet.h"

typedef struct OggTrack {
    uint32_t rate;
    uint32_t samples;
    uint32_t end_granule;
    uint32_t origin;
    uint32_t header_bytes;
    uint32_t serial;
    uint32_t data_offset;
    uint32_t file_bytes;
    uint16_t min_block;
    uint16_t max_block;
    uint16_t pre_skip;
    uint8_t channels;
} OggTrack;

bool ogg_track_endpoint(FlacInput input, OggTrack *track);
bool ogg_track_seek(FlacInput input, const OggTrack *track, uint32_t sample, uint32_t *offset);
