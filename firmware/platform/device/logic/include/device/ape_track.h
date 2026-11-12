#pragma once

#include <stddef.h>

#include "codec/flac.h"

enum {
    APE_LIMIT_BASE_MHZ = 100,
    APE_LIMIT_BOOST_MHZ = 200,
    APE_CONFIG_BYTES = 24,
    APE_RECORD_BYTES = 16,
    APE_VIRTUAL_END = 0x7FFFFFFF,
    APE_CHUNK_SAMPLES = 256,
    APE_INPUT_BYTES = 16384,
    APE_RING_BYTES = 65536
};

typedef struct ApeTrack {
    uint32_t seek_offset;
    uint32_t audio_start;
    uint32_t audio_end;
    uint32_t frames;
    uint32_t frame_samples;
    uint32_t final_samples;
    uint32_t samples;
    uint32_t rate;
    uint16_t version;
    uint16_t level;
    uint16_t bits;
    uint16_t channels;
} ApeTrack;

typedef struct ApeFrame {
    uint32_t offset;
    uint32_t bytes;
    uint32_t skip;
    uint32_t samples;
} ApeFrame;

bool ape_track_probe(FlacInput input, ApeTrack *track);
bool ape_track_frame(FlacInput input, const ApeTrack *track, uint32_t frame, ApeFrame *out);
static inline bool ape_track_allowed(const ApeTrack *track)
{
    return track != NULL && track->level >= 1000 && track->level <= 5000 &&
           track->level % 1000 == 0 && (track->bits == 16 || track->bits == 24) &&
           track->channels >= 1 && track->channels <= 2 && track->rate >= 8000 &&
           track->rate <= 192000;
}

uint32_t ape_track_clock(const ApeTrack *track);
bool ape_track_realtime(const ApeTrack *track, uint32_t clock_mhz);
