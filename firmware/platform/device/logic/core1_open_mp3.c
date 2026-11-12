#include <stddef.h>

#include "device/core1_source_private.h"
#include "device/mp3_probe.h"

enum {
    MP3_OUTPUT_BITS = 24,
    MILLISECONDS_PER_SECOND = 1000
};

bool core1_open_mp3(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    source->region.ring_bytes = core1_smaller(source->ring_capacity, MP3_RING_BYTES);
    FlacFileOpener files = source->flac.files;
    if (!files.open(files.context, path, &source->input)) {
        return false;
    }
    source->file_open = true;
    Mp3Stream mp3;
    if (!mp3_probe(source->input, &mp3)) {
        core1_source_stop(source);
        return false;
    }
    uint32_t playable = mp3_playable_samples(&mp3);
    uint32_t target = (uint32_t)((uint64_t)start_ms * mp3.sample_rate / MILLISECONDS_PER_SECOND);
    target = core1_smaller(target, playable);
    source->start_offset =
        target == 0 ? mp3.audio_start : mp3_seek_offset(&mp3, target + mp3.lead_trim);
    target += mp3.lead_trim;
    bool from_start = source->start_offset == mp3.audio_start;
    source->end_offset = mp3.audio_end;
    source->written = source->start_offset;
    source->lead_trim = mp3.lead_trim;
    source->skip_before = 0;
    source->play_end = UINT32_MAX;
    source->slot = 0;
    source->read_failed = false;
    source->bits_per_sample = MP3_OUTPUT_BITS;
    source->channels = mp3.channels;
    *track = (AudioTrackInfo){.stream = {.sample_rate = mp3.sample_rate,
                                         .channels = mp3.channels,
                                         .bits_per_sample = 0,
                                         .min_block = mp3.samples_per_frame,
                                         .max_block = mp3.samples_per_frame,
                                         .total_samples = playable},
                              .audio_bytes = mp3.audio_end - mp3.audio_start,
                              .codec = AUDIO_CODEC_MP3};
    volatile StreamControl *control = source->region.control;
    control->sample_rate = mp3.sample_rate;
    control->channels = mp3.channels;
    control->bits_per_sample = MP3_OUTPUT_BITS;
    control->min_block = mp3.samples_per_frame;
    control->max_block = mp3.samples_per_frame;
    control->total_samples = playable;
    control->tail_trim = mp3.tail_trim;
    control->first_sample = from_start ? 0 : target;
    control->start_offset = source->start_offset;
    control->end_offset = source->end_offset;
    control->written = source->written;
    core1_publish();
    control->generation = core1_source_next_generation(source);
    source->streaming = true;
    return true;
}
