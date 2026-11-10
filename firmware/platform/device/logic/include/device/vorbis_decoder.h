#pragma once

#include "device/vorbis_memory.h"

enum {
    VORBIS_CHANNELS = 2,
    VORBIS_MAX_FRAMES = 4096,
    VORBIS_WORKSPACE_BYTES = 65536
};

typedef struct VorbisSpan {
    const uint8_t *bytes;
    uint32_t size;
} VorbisSpan;

typedef struct VorbisPacket {
    VorbisSpan span[2];
    uint64_t granule;
    bool first;
    bool last;
} VorbisPacket;

typedef struct VorbisPcm {
    int32_t *channels[VORBIS_CHANNELS];
    uint32_t capacity;
    uint32_t frames;
} VorbisPcm;

typedef struct VorbisDecoder {
    VorbisMemory memory;
    void *info;
    void *comment;
    void *dsp;
    uint32_t packet_number;
    uint32_t rate;
    uint8_t channels;
    bool ready;
    bool ended;
    bool failed;
} VorbisDecoder;

bool vorbis_decoder_init(VorbisDecoder *decoder, void *workspace, uint32_t capacity);
bool vorbis_decoder_header(VorbisDecoder *decoder, VorbisPacket packet);
bool vorbis_decoder_decode(VorbisDecoder *decoder, VorbisPacket packet, VorbisPcm *pcm);
