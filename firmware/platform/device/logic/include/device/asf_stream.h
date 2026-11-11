#pragma once

#include "device/asf_track.h"

typedef struct AsfStream {
    FlacInput input;
    AsfTrack track;
    uint32_t packet_index;
    uint32_t cursor;
    uint32_t packet_end;
    uint32_t payloads;
    uint32_t payload_length_type;
    uint32_t properties;
    uint32_t compressed_cursor;
    uint32_t compressed_end;
    uint32_t compressed_timestamp;
    uint32_t compressed_delta;
    uint32_t object_bytes;
    uint32_t object_expected;
    uint32_t object_number;
    uint32_t object_timestamp;
    uint32_t first_sample;
    bool seeking;
    bool has_timestamp;
    uint32_t object_position;
    uint32_t position;
    uint32_t record_position;
    uint32_t record_bytes;
    uint8_t header[WMA_CONFIG_BYTES];
    bool configured;
    bool multiple;
    bool ended;
    bool failed;
    uint8_t packet[ASF_PACKET_BYTES];
    uint8_t object[ASF_PACKET_BYTES];
} AsfStream;

bool asf_stream_start(AsfStream *stream, FlacInput input, const AsfTrack *track);
bool asf_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length);

bool asf_stream_start_at(AsfStream *stream, FlacInput input, const AsfTrack *track,
                         uint32_t start_ms);
