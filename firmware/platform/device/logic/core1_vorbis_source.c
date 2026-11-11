#include "device/core1_source_private.h"

#include "device/vorbis_decoder.h"

static bool prepare(Core1Source *source, OggTrack *track, uint32_t start_ms)
{
    if (!source->wait_vorbis(source) || source->read_failed ||
        source->region.control->generation != source->generation ||
        source->region.control->vorbis_config_generation != source->generation) {
        return false;
    }
    core1_observe();
    VorbisModes modes = source->region.control->vorbis_modes;
    uint64_t desired = (uint64_t)start_ms * track->rate / 1000;
    uint32_t target = desired < track->samples ? (uint32_t)desired : track->samples;
    if (!ogg_stream_prepare_vorbis(source->ogg, track, &modes, target)) {
        return false;
    }
    source->skip_before = core1_smaller(target, track->samples);
    source->play_end = track->samples;
    source->region.control->first_sample = source->ogg->seek_offset == 0 ? 0 : source->skip_before;
    source->region.control->total_samples = track->samples;
    core1_publish();
    source->region.control->vorbis_start_generation = source->generation;
    source->end_offset = OGG_VIRTUAL_END;
    return true;
}

bool core1_open_vorbis(Core1Source *source, OggTrack *track, uint32_t start_ms)
{
    if (source->wait_vorbis == NULL || source->ring_capacity < VORBIS_RING_BYTES ||
        !ogg_stream_start(source->ogg, source->input)) {
        return false;
    }
    core1_ogg_begin(source, track,
                    (Core1OggStart){.ring_bytes = VORBIS_RING_BYTES,
                                    .end_offset = track->header_bytes,
                                    .first_sample = 0});
    return prepare(source, track, start_ms);
}
