#include "device/core1_source_private.h"

bool core1_open_flac(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    source->region.ring_bytes = source->ring_capacity;
    if (!flac_source_open_at(&source->flac, path, start_ms, track)) {
        return false;
    }
    const FlacInfo *info = &track->stream;
    FlacDecoder *decoder = source->flac.decoder;
    source->input = decoder->bits.input;
    source->start_offset = flac_position(decoder);
    source->end_offset = source->input.size;
    source->written = source->start_offset;
    source->lead_trim = 0;
    source->skip_before = 0;
    source->play_end = UINT32_MAX;
    source->slot = 0;
    source->read_failed = false;
    source->bits_per_sample = info->bits_per_sample;
    source->channels = info->channels;
    volatile StreamControl *control = source->region.control;
    control->sample_rate = info->sample_rate;
    control->channels = info->channels;
    control->bits_per_sample = info->bits_per_sample;
    control->min_block = info->min_block;
    control->max_block = info->max_block;
    control->total_samples = info->total_samples;
    control->tail_trim = 0;
    control->first_sample = 0;
    control->start_offset = source->start_offset;
    control->end_offset = source->end_offset;
    control->written = source->written;
    core1_publish();
    control->generation = core1_source_next_generation(source);
    source->streaming = true;
    return true;
}
