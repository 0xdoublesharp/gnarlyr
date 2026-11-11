#pragma once

#include <stdio.h>

#include "device/core1_opus.h"
#include "device/core1_vorbis.h"
#include "device/ogg_stream.h"

typedef struct SimOgg {
    FILE *file;
    OggTrack track;
    OggStream stream;
    StreamControl control;
    union {
        Core1Vorbis vorbis;
        Core1Opus opus;
    } codec;
    Core1Decoder decoder;
    Core1Block block;
    uint32_t taken;
    uint32_t next;
    bool failed;
    int32_t samples[STREAM_SLOTS][FLAC_MAX_CHANNELS][FLAC_MAX_BLOCK];
    _Alignas(8) uint8_t ring[VORBIS_RING_BYTES + VORBIS_WORKSPACE_BYTES];
} SimOgg;

bool sim_ogg_open(SimOgg *ogg, const char *path);
bool sim_ogg_seek(SimOgg *ogg, uint64_t frame);
uint64_t sim_ogg_read(SimOgg *ogg, float *frames, uint64_t count);
void sim_ogg_close(SimOgg *ogg);
