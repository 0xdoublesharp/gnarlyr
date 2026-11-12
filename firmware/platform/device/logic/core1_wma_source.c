#include "device/core1_source_private.h"

#include "device/asf_stream.h"

enum {
    WMA_OUTPUT_BITS = 29
};

static void publish(Core1Source *source, const AsfTrack *wma)
{
    volatile StreamControl *control = source->region.control;
    control->sample_rate = wma->rate;
    control->channels = wma->channels;
    control->bits_per_sample = WMA_OUTPUT_BITS;
    control->min_block = wma->frame_samples;
    control->max_block = wma->frame_samples;
    control->total_samples = wma->samples;
    control->tail_trim = 0;
    control->first_sample = source->asf->first_sample;
    control->start_offset = 0;
    control->end_offset = WMA_VIRTUAL_END;
    control->written = 0;
    core1_publish();
    control->generation = core1_source_next_generation(source);
    source->streaming = true;
}

bool core1_open_wma(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    FlacFileOpener files = source->flac.files;
    if (!files.open(files.context, path, &source->input)) {
        return false;
    }
    source->file_open = true;
    AsfTrack wma;
    if (source->ring_capacity < WMA_RING_BYTES || !asf_track_probe(source->input, &wma) ||
        !asf_stream_start_at(source->asf, source->input, &wma, start_ms)) {
        core1_source_stop(source);
        return false;
    }
    uint64_t desired = (uint64_t)start_ms * wma.rate / 1000;
    uint32_t target = desired < wma.samples ? (uint32_t)desired : wma.samples;
    source->input = (FlacInput){asf_stream_read, source->asf, WMA_VIRTUAL_END};
    source->region.ring_bytes = WMA_RING_BYTES;
    source->start_offset = 0;
    source->end_offset = WMA_VIRTUAL_END;
    source->written = 0;
    source->lead_trim = 0;
    source->skip_before = target;
    source->play_end = UINT32_MAX;
    source->slot = 0;
    source->read_failed = false;
    source->bits_per_sample = WMA_OUTPUT_BITS;
    source->channels = (uint8_t)wma.channels;
    *track = (AudioTrackInfo){.stream = {.sample_rate = wma.rate,
                                         .channels = (uint8_t)wma.channels,
                                         .min_block = (uint16_t)wma.frame_samples,
                                         .max_block = (uint16_t)wma.frame_samples,
                                         .total_samples = wma.samples},
                              .audio_bytes = wma.data_end - wma.data_offset,
                              .codec = AUDIO_CODEC_WMA};
    publish(source, &wma);
    return true;
}
