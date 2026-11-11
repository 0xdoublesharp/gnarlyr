#include "device/core1_source_private.h"

#include "device/core1_opus.h"
#include "device/opus_track.h"

bool core1_open_opus(Core1Source *source, const OggTrack *track, uint32_t start_ms)
{
    uint64_t desired = (uint64_t)start_ms * (OPUS_TRACK_RATE / 1000);
    uint32_t target = desired < track->samples ? (uint32_t)desired : track->samples;
    uint32_t offset;
    if (source->ring_capacity < OPUS_RING_BYTES ||
        !opus_track_seek(source->input, track, target, &offset) ||
        !ogg_stream_resume(source->ogg, source->input, track, offset)) {
        return false;
    }
    core1_ogg_begin(source, track,
                    (Core1OggStart){.ring_bytes = OPUS_RING_BYTES,
                                    .end_offset = OGG_VIRTUAL_END,
                                    .first_sample = offset == 0 ? 0 : target});
    source->skip_before = target;
    return true;
}
