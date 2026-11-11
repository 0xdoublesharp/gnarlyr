#pragma once

#include "codec/ogg_packet.h"

typedef struct VorbisTrack {
    uint32_t rate;
    uint32_t samples;
    uint32_t serial;
    uint32_t data_offset;
    uint32_t file_bytes;
    uint16_t max_block;
    uint8_t channels;
} VorbisTrack;

bool vorbis_track_probe(FlacInput input, VorbisTrack *track);
bool vorbis_track_seek(FlacInput input, const VorbisTrack *track, uint32_t sample,
                       uint32_t *offset);
