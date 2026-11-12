#pragma once

#include "device/ape_track.h"

typedef struct ApeStream {
    FlacInput input;
    ApeTrack track;
    ApeFrame current;
    uint32_t frame;
    uint32_t position;
    uint32_t record_position;
    uint32_t record_bytes;
    uint8_t header[APE_CONFIG_BYTES];
    bool configured;
    bool ended;
    bool failed;
} ApeStream;

bool ape_stream_start(ApeStream *stream, FlacInput input, const ApeTrack *track, uint32_t frame);
bool ape_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length);
