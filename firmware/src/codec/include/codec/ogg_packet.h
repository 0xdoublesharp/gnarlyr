#pragma once

#include "codec/flac.h"

enum {
    OGG_PAGE_SEGMENTS = 255
};

typedef enum OggPacketResult {
    OGG_PACKET_READY,
    OGG_PACKET_END,
    OGG_PACKET_WAIT,
    OGG_PACKET_INVALID,
    OGG_PACKET_TOO_LARGE
} OggPacketResult;

typedef struct OggPacket {
    uint32_t bytes;
    uint64_t granule;
    bool first;
    bool last;
} OggPacket;

typedef struct OggPackets {
    FlacInput input;
    uint32_t next_page;
    uint32_t body;
    uint32_t serial;
    uint32_t sequence;
    uint64_t granule;
    uint16_t segments;
    uint16_t segment;
    uint16_t last_complete;
    uint8_t flags;
    uint8_t laces[OGG_PAGE_SEGMENTS];
    bool started;
    bool ended;
    bool emitted;
} OggPackets;

void ogg_packets_init(OggPackets *reader, FlacInput input);
OggPacketResult ogg_packets_next(OggPackets *reader, uint8_t *buffer, uint32_t capacity,
                                 OggPacket *packet);

OggPacketResult ogg_packets_prefix(OggPackets *reader, uint8_t *buffer, uint32_t capacity,
                                   OggPacket *packet);
