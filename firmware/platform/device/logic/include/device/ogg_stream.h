#pragma once

#include "codec/ogg_packet.h"
#include "device/ogg_record.h"
#include "device/vorbis_track.h"

enum {
    OGG_VIRTUAL_END = 0x7FFFFFFF
};

typedef struct OggStream {
    OggPackets packets;
    uint32_t position;
    uint32_t record_done;
    uint32_t record_size;
    uint32_t seek_offset;
    uint32_t serial;
    uint32_t origin;
    bool origin_sent;
    uint8_t headers;
    bool ended;
    bool failed;
    bool seeked;
    bool opus;
    uint8_t head[OGG_RECORD_BYTES];
    uint8_t body[OGG_PACKET_BYTES];
} OggStream;

bool ogg_stream_start(OggStream *stream, FlacInput file);
bool ogg_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length);

bool ogg_stream_resume(OggStream *stream, FlacInput file, const OggTrack *track, uint32_t offset);
bool ogg_stream_seek(OggStream *stream, FlacInput file, const OggTrack *track, uint32_t sample);

bool ogg_stream_prepare_vorbis(OggStream *stream, OggTrack *track, const VorbisModes *modes,
                               uint32_t sample);
