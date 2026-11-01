#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "codec/flac.h"
#include "device/mp4_demux.h"

enum {
    M4A_RECORD_HEADER_BYTES = 4,
    M4A_HEAD_BYTES = M4A_RECORD_HEADER_BYTES + 1 + MP4_CONFIG_BYTES,
    M4A_RING_BYTES = 0x10000,
    M4A_VIRTUAL_END = 0x7FFFFFFF
};

typedef struct M4aStream {
    FlacInput file;
    Mp4Track track;
    Mp4Cursor cursor;
    uint32_t position;
    uint32_t record_done;
    uint32_t head_size;
    uint32_t body_size;
    uint32_t body_offset;
    bool configured;
    bool ended;
    uint8_t head[M4A_HEAD_BYTES];
} M4aStream;

bool m4a_stream_start(M4aStream *stream, FlacInput file, const Mp4Track *track,
                      uint32_t first_sample);
bool m4a_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length);
