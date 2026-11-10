#pragma once

#include "codec/ogg_packet.h"
#include "device/vorbis_record.h"

enum {
    VORBIS_VIRTUAL_END = 0x7FFFFFFF
};

typedef struct VorbisStream {
    OggPackets packets;
    uint32_t position;
    uint32_t record_done;
    uint32_t record_size;
    uint8_t headers;
    bool ended;
    bool failed;
    uint8_t head[VORBIS_RECORD_BYTES];
    uint8_t body[VORBIS_PACKET_BYTES];
} VorbisStream;

bool vorbis_stream_start(VorbisStream *stream, FlacInput file);
bool vorbis_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length);
