#include "device/core1_source_private.h"

#include <stddef.h>

#include "device/opus_track.h"
#include "device/vorbis_track.h"

void core1_ogg_begin(Core1Source *source, const OggTrack *track, Core1OggStart start)
{
    source->input = (FlacInput){core1_ogg_read, source->ogg, OGG_VIRTUAL_END};
    source->start_offset = 0;
    source->end_offset = start.end_offset;
    source->written = 0;
    source->slot = 0;
    source->read_failed = false;
    source->lead_trim = 0;
    source->skip_before = 0;
    source->play_end = track->samples;
    source->bits_per_sample = 24;
    source->channels = track->channels;
    source->region.ring_bytes = start.ring_bytes;
    volatile StreamControl *control = source->region.control;
    control->sample_rate = track->rate;
    control->channels = track->channels;
    control->bits_per_sample = 24;
    control->min_block = track->min_block;
    control->max_block = track->max_block;
    control->total_samples = track->samples;
    control->tail_trim = 0;
    control->first_sample = start.first_sample;
    control->start_offset = 0;
    control->end_offset = OGG_VIRTUAL_END;
    control->written = 0;
    control->vorbis_config_generation = 0;
    control->vorbis_start_generation = 0;
    core1_publish();
    control->generation = core1_source_next_generation(source);
    source->streaming = true;
}

static bool open_codec(Core1Source *source, uint32_t start_ms, OggTrack *track)
{
    if (opus_track_probe(source->input, track)) {
        return core1_open_opus(source, track, start_ms);
    }
    return vorbis_track_probe(source->input, track) && core1_open_vorbis(source, track, start_ms);
}

bool core1_open_ogg(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    FlacFileOpener files = source->flac.files;
    if (source->ogg == NULL || !files.open(files.context, path, &source->input)) {
        return false;
    }
    source->file_open = true;
    OggTrack ogg;
    if (!open_codec(source, start_ms, &ogg)) {
        core1_source_stop(source);
        return false;
    }
    *track = (AudioTrackInfo){.stream = {.sample_rate = ogg.rate,
                                         .channels = ogg.channels,
                                         .min_block = ogg.min_block,
                                         .max_block = ogg.max_block,
                                         .total_samples = ogg.samples},
                              .audio_bytes = ogg.file_bytes - ogg.data_offset,
                              .codec = ogg.opus ? AUDIO_CODEC_OPUS : AUDIO_CODEC_VORBIS};
    return true;
}
