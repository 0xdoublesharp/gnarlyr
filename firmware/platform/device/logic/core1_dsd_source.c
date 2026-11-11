#include "device/dsd_stream.h"

#include "device/core1_source_private.h"

bool core1_open_dsd(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    FlacFileOpener files = source->flac.files;
    if (source->dsd == NULL || source->ring_capacity < DSD_RING_BYTES ||
        !files.open(files.context, path, &source->input)) {
        return false;
    }
    source->file_open = true;
    AudioTrackInfo candidate;
    if (!dsd_stream_start(source->dsd, source->input, start_ms, &candidate) ||
        (source->dsd->track.rate == 11289600 && source->dsd->track.channels != 1)) {
        core1_source_stop(source);
        return false;
    }
    uint32_t end = DSD_HEADER_BYTES + source->dsd->bytes;
    source->input = (FlacInput){.read = dsd_stream_read, .context = source->dsd, .size = end};
    source->start_offset = 0;
    source->end_offset = end;
    source->written = 0;
    source->lead_trim = 0;
    uint64_t target = (uint64_t)start_ms * DSD_PCM_RATE / 1000;
    source->skip_before = target < candidate.stream.total_samples
                              ? (uint32_t)target
                              : (uint32_t)candidate.stream.total_samples;
    source->play_end = (uint32_t)candidate.stream.total_samples;
    source->slot = 0;
    source->read_failed = false;
    source->bits_per_sample = 24;
    source->channels = candidate.stream.channels;
    source->region.ring_bytes = DSD_RING_BYTES;
    volatile StreamControl *control = source->region.control;
    control->sample_rate = DSD_PCM_RATE;
    control->channels = source->channels;
    control->bits_per_sample = 24;
    control->min_block = DSD_OUTPUT_FRAMES;
    control->max_block = DSD_OUTPUT_FRAMES;
    control->total_samples = (uint32_t)candidate.stream.total_samples;
    control->tail_trim = 0;
    control->first_sample = 0;
    control->start_offset = 0;
    control->end_offset = end;
    control->written = 0;
    core1_publish();
    control->generation = core1_source_next_generation(source);
    source->streaming = true;
    *track = candidate;
    return true;
}
