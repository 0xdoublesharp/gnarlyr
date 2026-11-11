#pragma once

#include "device/ogg_track.h"
#include "device/vorbis_modes.h"

bool vorbis_track_probe(FlacInput input, OggTrack *track);
bool vorbis_track_origin(const OggPackets *audio, const VorbisModes *modes, OggTrack *track);
