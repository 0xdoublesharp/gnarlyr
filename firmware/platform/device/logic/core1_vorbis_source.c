#include "device/core1_source_private.h"

#include <stddef.h>

#include "device/vorbis_decoder.h"

static bool failed(Core1Source *source)
{
    core1_source_stop(source);
    return false;
}

static void begin(Core1Source *source, const OggTrack *track)
{
    source->input = (FlacInput){core1_ogg_read, source->ogg, OGG_VIRTUAL_END};
    source->start_offset = 0;
    source->end_offset = track->header_bytes;
    source->written = 0;
    source->slot = 0;
    source->read_failed = false;
    source->lead_trim = 0;
    source->skip_before = 0;
    source->play_end = track->samples;
    source->bits_per_sample = 24;
    source->channels = track->channels;
    source->region.ring_bytes = VORBIS_RING_BYTES;
    volatile StreamControl *control = source->region.control;
    control->sample_rate = track->rate;
    control->channels = track->channels;
    control->bits_per_sample = 24;
    control->min_block = track->min_block;
    control->max_block = track->max_block;
    control->total_samples = track->samples;
    control->tail_trim = 0;
    control->first_sample = 0;
    control->start_offset = 0;
    control->end_offset = OGG_VIRTUAL_END;
    control->written = 0;
    control->vorbis_config_generation = 0;
    control->vorbis_start_generation = 0;
    core1_publish();
    control->generation = core1_source_next_generation(source);
    source->streaming = true;
}

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

bool core1_open_ogg(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    if (source->ogg == NULL || source->wait_vorbis == NULL ||
        source->ring_capacity < VORBIS_RING_BYTES) {
        return false;
    }
    FlacFileOpener files = source->flac.files;
    if (!files.open(files.context, path, &source->input)) {
        return false;
    }
    source->file_open = true;
    OggTrack vorbis;
    if (!vorbis_track_probe(source->input, &vorbis) ||
        !ogg_stream_start(source->ogg, source->input)) {
        return failed(source);
    }
    begin(source, &vorbis);
    if (!prepare(source, &vorbis, start_ms)) {
        return failed(source);
    }
    *track = (AudioTrackInfo){.stream = {.sample_rate = vorbis.rate,
                                         .channels = vorbis.channels,
                                         .min_block = vorbis.min_block,
                                         .max_block = vorbis.max_block,
                                         .total_samples = vorbis.samples},
                              .audio_bytes = vorbis.file_bytes - vorbis.data_offset,
                              .codec = AUDIO_CODEC_VORBIS};
    return true;
}
