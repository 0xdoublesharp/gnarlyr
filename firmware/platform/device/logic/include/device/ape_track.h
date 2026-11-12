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

typedef struct ApePlaybackLimit {
    uint16_t level;
    uint32_t mono_rate;
    uint32_t stereo_rate;
    uint32_t boosted_stereo_rate;
} ApePlaybackLimit;

static inline uint32_t ape_track_clock_for(const ApePlaybackLimit *limit, const ApeTrack *track)
{
    bool mono = track->channels == 1;
    uint32_t base = mono ? limit->mono_rate : limit->stereo_rate;
    uint32_t boosted = mono ? limit->mono_rate : limit->boosted_stereo_rate;
    return track->rate <= base      ? APE_LIMIT_BASE_MHZ
           : track->rate <= boosted ? APE_LIMIT_BOOST_MHZ
                                    : 0;
}

static inline uint32_t ape_track_clock(const ApeTrack *track)
{
    static const ApePlaybackLimit LIMITS[] = {
        {1000, 192000, 96000, 192000},
        {2000, 192000, 48000, 96000},
        {3000, 96000, 48000, 48000},
    };
    if (!ape_track_allowed(track)) {
        return 0;
    }
    for (uint32_t index = 0; index < sizeof LIMITS / sizeof LIMITS[0]; index++) {
        if (track->level == LIMITS[index].level) {
            return ape_track_clock_for(&LIMITS[index], track);
        }
    }
    return 0;
}

bool ape_track_realtime(const ApeTrack *track, uint32_t clock_mhz);
