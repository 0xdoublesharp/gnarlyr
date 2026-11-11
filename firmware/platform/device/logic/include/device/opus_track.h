#pragma once

#include "device/ogg_track.h"

enum {
    OPUS_TRACK_RATE = 48000,
    OPUS_PRE_ROLL = 3840,
    OPUS_PACKET_FRAMES = 5760
};

uint32_t opus_track_frames(const uint8_t *toc, uint32_t bytes);
bool opus_track_probe(FlacInput input, OggTrack *track);
bool opus_track_seek(FlacInput input, const OggTrack *track, uint32_t sample, uint32_t *offset);
