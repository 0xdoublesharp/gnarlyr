#include "device/core1_stream.h"

#include <stddef.h>

#include "core/seam.h"
#include "device/core1_source_private.h"
#include "device/dsd_stream.h"

void core1_publish(void)
{
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

void core1_observe(void)
{
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

uint32_t core1_smaller(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static void start_flac(void *context, FlacInput input, FlacInfo info, uint32_t first_sample,
                       const bool *starved)
{
    Core1Flac *flac = context;
    (void)first_sample;
    flac->starved = starved;
    flac_start(flac->decoder, input, info);
}

static Core1DecodeResult decode_flac(void *context, Core1Block *block)
{
    Core1Flac *flac = context;
    FlacDecoder *decoder = flac->decoder;
    uint32_t frame_start = flac_position(decoder);
    FlacResult result = flac_decode_frame(decoder);
    if (*flac->starved) {
        flac_rewind(decoder, frame_start);
        return CORE1_STARVED;
    }
    if (result == FLAC_END) {
        return CORE1_FINISHED;
    }
    if (result != FLAC_OK) {
        return CORE1_FAILED;
    }
    for (int channel = 0; channel < FLAC_MAX_CHANNELS; channel++) {
        block->channels[channel] =
            flac_channel(decoder, channel < decoder->info.channels ? channel : 0);
    }
    block->first_sample = decoder->frame_first_sample;
    block->samples = decoder->block_samples;
    block->position = flac_position(decoder);
    return CORE1_DECODED;
}

Core1Decoder core1_flac_decoder(Core1Flac *flac, FlacDecoder *decoder)
{
    *flac = (Core1Flac){.decoder = decoder, .starved = NULL};
    return (Core1Decoder){start_flac, decode_flac, flac};
}

void core1_stream_init(Core1Stream *stream, StreamRegion region, Core1Decoder decoder)
{
    *stream = (Core1Stream){.region = region, .decoder = decoder};
}

void core1_stream_clock(StreamRegion region, uint32_t mhz)
{
    if (region.request_clock != NULL) {
        region.request_clock(mhz);
    }
}

static void copy_from_ring(const StreamRegion *region, uint32_t position, uint8_t *out,
                           uint32_t length)
{
    uint32_t first = position % region->ring_bytes;
    uint32_t part = core1_smaller(length, region->ring_bytes - first);
    for (uint32_t index = 0; index < part; index++) {
        out[index] = region->ring[first + index];
    }
    for (uint32_t index = part; index < length; index++) {
        out[index] = region->ring[index - part];
    }
}

static bool read_ring(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    Core1Stream *stream = context;
    volatile StreamControl *control = stream->region.control;
    if (control->generation != stream->generation) {
        return false;
    }
    uint32_t position = stream->start_offset + offset;
    if (position + length > control->written) {
        stream->starved = true;
        return false;
    }
    core1_observe();
    copy_from_ring(&stream->region, position, buffer, length);
    return true;
}

static void adopt(Core1Stream *stream, uint32_t generation)
{
    core1_stream_clock(stream->region, 100);
    volatile StreamControl *control = stream->region.control;
    core1_observe();
    FlacInfo info = {
        .sample_rate = control->sample_rate,
        .channels = (uint8_t)control->channels,
        .bits_per_sample = (uint8_t)control->bits_per_sample,
        .min_block = (uint16_t)control->min_block,
        .max_block = (uint16_t)control->max_block,
        .total_samples = control->total_samples,
    };
    stream->generation = generation;
    stream->start_offset = control->start_offset;
    stream->end_offset = control->end_offset;
    stream->channels =
        control->channels < FLAC_MAX_CHANNELS ? control->channels : FLAC_MAX_CHANNELS;
    stream->slot = 0;
    stream->finished = false;
    stream->starved = false;
    FlacInput input = {
        .read = read_ring, .context = stream, .size = stream->end_offset - stream->start_offset};
    stream->decoder.start(stream->decoder.context, input, info, control->first_sample,
                          &stream->starved);
    control->released = stream->start_offset;
    core1_publish();
    control->acked_generation = generation;
}

static void copy_channels(Core1Stream *stream, uint32_t slot, const Core1Block *block)
{
    for (uint32_t channel = 0; channel < stream->channels; channel++) {
        const int32_t *from = block->channels[channel];
        int32_t *to = stream->region.samples[slot][channel];
        if (from == to) {
            continue;
        }
        for (uint32_t index = 0; index < block->samples; index++) {
            to[index] = from[index];
        }
    }
}

static void fill_slot(Core1Stream *stream, Core1DecodeResult result, const Core1Block *block)
{
    volatile StreamControl *control = stream->region.control;
    volatile StreamSlot *slot = &control->slots[stream->slot];
    uint32_t outcome = AUDIO_FRAME_ERROR;
    if (result == CORE1_DECODED) {
        copy_channels(stream, stream->slot, block);
        slot->first_sample = block->first_sample;
        slot->samples = block->samples;
        control->released = stream->start_offset + block->position;
        outcome = AUDIO_FRAME_READY;
    } else {
        slot->first_sample = 0;
        slot->samples = 0;
        outcome = result == CORE1_FINISHED ? AUDIO_FRAME_END : AUDIO_FRAME_ERROR;
        stream->finished = true;
    }
    slot->generation = stream->generation;
    slot->result = outcome;
    core1_publish();
    slot->state = STREAM_SLOT_FULL;
    stream->slot = (stream->slot + 1) % STREAM_SLOTS;
}

bool core1_stream_step(Core1Stream *stream)
{
    volatile StreamControl *control = stream->region.control;
    uint32_t generation = control->generation;
    if (generation != stream->generation) {
        if (generation == 0) {
            core1_stream_clock(stream->region, 100);
            stream->generation = 0;
            return false;
        }
        adopt(stream, generation);
        return true;
    }
    if (generation == 0 || stream->finished) {
        return false;
    }
    if (control->slots[stream->slot].state != STREAM_SLOT_EMPTY) {
        return false;
    }
    stream->starved = false;
    Core1Block block = {0};
    Core1DecodeResult result = stream->decoder.decode(stream->decoder.context, &block);
    if (control->generation != stream->generation) {
        return true;
    }
    if (result == CORE1_STARVED) {
        stream->starved = false;
        return false;
    }
    fill_slot(stream, result, &block);
    return true;
}

CORE_COLD void core1_source_init(Core1Source *source, StreamRegion region, FlacFileOpener files,
                                 FlacDecoder *decoder, uint32_t fill_budget)
{
    *source = (Core1Source){.region = region,
                            .fill_budget = fill_budget,
                            .ring_capacity = region.ring_bytes,
                            .clock_mhz = APE_LIMIT_BASE_MHZ};
    flac_source_init(&source->flac, files, decoder);
}

CORE_COLD void core1_source_stop(Core1Source *source)
{
    source->refilling = false;
    if (source->streaming) {
        source->region.control->generation = 0;
        source->streaming = false;
    }
    flac_source_close(&source->flac);
    if (source->file_open) {
        source->flac.files.close(source->flac.files.context);
        source->file_open = false;
    }
}

uint32_t core1_source_next_generation(Core1Source *source)
{
    source->generation++;
    if (source->generation == 0) {
        source->generation = 1;
    }
    return source->generation;
}

static void drop_stale_slots(Core1Source *source)
{
    volatile StreamControl *control = source->region.control;
    for (uint32_t index = 0; index < STREAM_SLOTS; index++) {
        volatile StreamSlot *slot = &control->slots[index];
        if (slot->state == STREAM_SLOT_FULL && slot->generation != source->generation) {
            slot->state = STREAM_SLOT_EMPTY;
        }
    }
}

CORE_SEAM void core1_source_fill(Core1Source *source)
{
    if (!source->streaming || source->read_failed) {
        return;
    }
    drop_stale_slots(source);
    volatile StreamControl *control = source->region.control;
    uint32_t released =
        control->acked_generation == source->generation ? control->released : source->start_offset;
    core1_observe();
    if (!source->refilling && control->acked_generation == source->generation &&
        source->written - released > source->region.ring_bytes / 2) {
        return;
    }
    uint32_t target = core1_smaller(released + source->region.ring_bytes, source->end_offset);
    uint32_t budget = source->fill_budget;
    while (source->written < target && budget > 0) {
        uint32_t position = source->written % source->region.ring_bytes;
        uint32_t length = core1_smaller(core1_smaller(target - source->written, budget),
                                        source->region.ring_bytes - position);
        void *destination = (void *)(uintptr_t)&source->region.ring[position];
        if (!source->input.read(source->input.context, source->written, destination, length)) {
            source->read_failed = true;
            return;
        }
        source->written += length;
        budget -= length;
        core1_publish();
        control->written = source->written;
    }
    source->refilling = source->written < target;
}

static void advance_slot(Core1Source *source)
{
    source->region.control->slots[source->slot].state = STREAM_SLOT_EMPTY;
    source->slot = (source->slot + 1) % STREAM_SLOTS;
}

static AudioFrameResult next_frame(void *context, AudioFrame *frame)
{
    Core1Source *source = context;
    core1_source_fill(source);
    if (!source->streaming || source->read_failed) {
        return AUDIO_FRAME_ERROR;
    }
    for (;;) {
        drop_stale_slots(source);
        volatile StreamSlot *slot = &source->region.control->slots[source->slot];
        if (slot->state != STREAM_SLOT_FULL) {
            return AUDIO_FRAME_WAIT;
        }
        core1_observe();
        if (slot->result != AUDIO_FRAME_READY) {
            return slot->result == AUDIO_FRAME_END ? AUDIO_FRAME_END : AUDIO_FRAME_ERROR;
        }
        uint32_t first = slot->first_sample;
        if (first >= source->play_end) {
            return AUDIO_FRAME_END;
        }
        uint32_t end = core1_smaller(first + slot->samples, source->play_end);
        uint32_t from = first > source->lead_trim ? first : source->lead_trim;
        from = from > source->skip_before ? from : source->skip_before;
        if (from >= end) {
            advance_slot(source);
            continue;
        }
        uint32_t skip = from - first;
        int32_t (*channels)[FLAC_MAX_BLOCK] = source->region.samples[source->slot];
        *frame = (AudioFrame){
            .left = channels[0] + skip,
            .right = channels[source->channels > 1 ? 1 : 0] + skip,
            .first_sample = from - source->lead_trim,
            .samples = end - from,
            .bits = source->bits_per_sample,
        };
        return AUDIO_FRAME_READY;
    }
}

static void release_frame(void *context)
{
    advance_slot(context);
}

CORE_COLD static void close_stream(void *context)
{
    core1_source_stop(context);
}

AudioSource core1_source_interface(Core1Source *source, Core1Format format)
{
    static bool (*const OPENS[])(void *, const char *, uint32_t, AudioTrackInfo *) = {
        [CORE1_FORMAT_FLAC] = core1_open_flac, [CORE1_FORMAT_MP3] = core1_open_mp3,
        [CORE1_FORMAT_M4A] = core1_open_m4a,   [CORE1_FORMAT_VORBIS] = core1_open_ogg,
        [CORE1_FORMAT_OPUS] = core1_open_ogg,  [CORE1_FORMAT_DSD] = core1_open_dsd,
        [CORE1_FORMAT_APE] = core1_open_ape,   [CORE1_FORMAT_WMA] = core1_open_wma,
    };
    return (AudioSource){OPENS[format], next_frame, release_frame, close_stream, source};
}

bool core1_ogg_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    return ogg_stream_read(context, offset, buffer, length);
}
