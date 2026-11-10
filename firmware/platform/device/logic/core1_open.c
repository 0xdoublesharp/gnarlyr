#include <stddef.h>

#include "device/core1_source_private.h"
#include "device/mp3_probe.h"
#include "device/mp4_demux.h"

enum {
    MP3_OUTPUT_BITS = 24,
    MILLISECONDS_PER_SECOND = 1000
};

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

static uint32_t m4a_first_packet(const Mp4Track *mp4, uint32_t target)
{
    uint32_t packet = (target + mp4->delay) / mp4->frame_samples;
    if (packet >= mp4->sample_count) {
        packet = mp4->sample_count - 1;
    }
    return mp4->codec == MP4_CODEC_AAC && packet > 0 ? packet - 1 : packet;
}

static bool begin_m4a(Core1Source *source, const Mp4Track *mp4, uint32_t packet)
{
    FlacInput file = source->input;
    if (mp4->sample_count == 0 || !m4a_stream_start(source->m4a, file, mp4, packet)) {
        return false;
    }
    source->input =
        (FlacInput){.read = m4a_stream_read, .context = source->m4a, .size = M4A_VIRTUAL_END};
    source->start_offset = 0;
    source->end_offset = M4A_VIRTUAL_END;
    source->written = 0;
    source->lead_trim = mp4->delay;
    source->play_end = mp4->delay + mp4->valid_samples;
    source->slot = 0;
    source->read_failed = false;
    source->bits_per_sample = mp4->codec == MP4_CODEC_AAC ? MP3_OUTPUT_BITS : mp4->bits;
    source->channels = mp4->channels;
    source->region.ring_bytes = core1_smaller(source->ring_capacity, M4A_RING_BYTES);
    return true;
}

static void publish_m4a(Core1Source *source, const Mp4Track *mp4, uint32_t first_sample)
{
    volatile StreamControl *control = source->region.control;
    control->sample_rate = mp4->sample_rate;
    control->channels = mp4->channels;
    control->bits_per_sample = source->bits_per_sample;
    control->min_block = (uint32_t)mp4->frame_samples;
    control->max_block = (uint32_t)mp4->frame_samples;
    control->total_samples = mp4->valid_samples;
    control->tail_trim = 0;
    control->first_sample = first_sample;
    control->start_offset = 0;
    control->end_offset = M4A_VIRTUAL_END;
    control->written = 0;
    core1_publish();
    control->generation = core1_source_next_generation(source);
    source->streaming = true;
}

static uint32_t m4a_start_packet(const Mp4Track *mp4, uint32_t start_ms)
{
    uint64_t target = (uint64_t)start_ms * mp4->sample_rate / MILLISECONDS_PER_SECOND;
    return m4a_first_packet(mp4,
                            target < mp4->valid_samples ? (uint32_t)target : mp4->valid_samples);
}

bool core1_open_m4a(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    FlacFileOpener files = source->flac.files;
    if (!files.open(files.context, path, &source->input)) {
        return false;
    }
    source->file_open = true;
    Mp4Track mp4;
    if (!mp4_probe(source->input, &mp4)) {
        core1_source_stop(source);
        return false;
    }
    uint32_t packet = m4a_start_packet(&mp4, start_ms);
    if (!begin_m4a(source, &mp4, packet)) {
        core1_source_stop(source);
        return false;
    }
    *track =
        (AudioTrackInfo){.stream = {.sample_rate = mp4.sample_rate,
                                    .channels = mp4.channels,
                                    .bits_per_sample = mp4.codec == MP4_CODEC_AAC ? 0 : mp4.bits,
                                    .min_block = (uint16_t)mp4.frame_samples,
                                    .max_block = (uint16_t)mp4.frame_samples,
                                    .total_samples = mp4.valid_samples},
                         .audio_bytes = mp4.audio_bytes,
                         .codec = mp4.codec == MP4_CODEC_AAC ? AUDIO_CODEC_AAC : AUDIO_CODEC_ALAC};
    publish_m4a(source, &mp4, packet * mp4.frame_samples);
    return true;
}
