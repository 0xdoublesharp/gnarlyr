#include "device/core1_source_private.h"

#include "device/ape_stream.h"

static void publish(Core1Source *source, const ApeTrack *ape, uint32_t frame)
{
    volatile StreamControl *control = source->region.control;
    control->sample_rate = ape->rate;
    control->channels = ape->channels;
    control->bits_per_sample = ape->bits;
    control->min_block = APE_CHUNK_SAMPLES;
    control->max_block = APE_CHUNK_SAMPLES;
    control->total_samples = ape->samples;
    control->tail_trim = 0;
    control->first_sample = frame * ape->frame_samples;
    control->start_offset = 0;
    control->end_offset = APE_VIRTUAL_END;
    control->written = 0;
    core1_publish();
    control->generation = core1_source_next_generation(source);
    source->streaming = true;
}

bool core1_open_ape(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    FlacFileOpener files = source->flac.files;
    if (!files.open(files.context, path, &source->input)) {
        return false;
    }
    source->file_open = true;
    ApeTrack ape;
    if (source->ring_capacity < APE_RING_BYTES || !ape_track_probe(source->input, &ape) ||
        !ape_track_realtime(&ape, source->clock_mhz)) {
        core1_source_stop(source);
        return false;
    }
    uint64_t desired = (uint64_t)start_ms * ape.rate / 1000;
    uint32_t target = desired < ape.samples ? (uint32_t)desired : ape.samples;
    uint32_t frame = target / ape.frame_samples;
    frame = frame < ape.frames ? frame : ape.frames - 1;
    if (!ape_stream_start(source->ape, source->input, &ape, frame)) {
        core1_source_stop(source);
        return false;
    }
    source->input = (FlacInput){ape_stream_read, source->ape, APE_VIRTUAL_END};
    source->region.ring_bytes = APE_RING_BYTES;
    source->start_offset = 0;
    source->end_offset = APE_VIRTUAL_END;
    source->written = 0;
    source->lead_trim = 0;
    source->skip_before = target;
    source->play_end = ape.samples;
    source->slot = 0;
    source->read_failed = false;
    source->bits_per_sample = (uint8_t)ape.bits;
    source->channels = (uint8_t)ape.channels;
    *track = (AudioTrackInfo){.stream = {.sample_rate = ape.rate,
                                         .channels = (uint8_t)ape.channels,
                                         .bits_per_sample = (uint8_t)ape.bits,
                                         .min_block = APE_CHUNK_SAMPLES,
                                         .max_block = APE_CHUNK_SAMPLES,
                                         .total_samples = ape.samples},
                              .audio_bytes = ape.audio_end - ape.audio_start,
                              .codec = AUDIO_CODEC_APE};
    publish(source, &ape, frame);
    return true;
}
