#include "device/core1_opus.h"

#include "device/core1_ogg.h"
#include "device/core1_source_private.h"

static uint32_t little16(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8;
}

static void start_opus(void *context, FlacInput input, FlacInfo info, uint32_t first_sample,
                       const bool *starved)
{
    Core1Opus *opus = context;
    (void)info;
    (void)starved;
    opus->decoder = (OpusDecode){0};
    opus->input = input;
    opus->work = (OpusWorkspace *)(uintptr_t)(opus->region.ring + opus->region.ring_bytes);
    opus->position = 0;
    opus->generation = opus->region.control->generation;
    opus->origin = 0;
    opus->next = 0;
    opus->staged = 0;
    opus->taken = 0;
    opus->first = 0;
    opus->slot = 0;
    opus->origin_set = false;
    opus->audio_started = false;
    opus->anchored = first_sample == 0;
}

static bool signed_head(const uint8_t *head)
{
    static const uint8_t SIGNATURE[] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'};
    for (uint32_t index = 0; index < sizeof SIGNATURE; index++) {
        if (head[index] != SIGNATURE[index]) {
            return false;
        }
    }
    return true;
}

static bool configure(Core1Opus *opus, const OggRecord *record)
{
    const uint8_t *head = opus->work->packet;
    if (record->flags != OGG_RECORD_FIRST || record->bytes < OPUS_HEAD_BYTES ||
        !signed_head(head) || head[8] > 15 || head[9] < 1 || head[9] > 2 || head[18] != 0 ||
        head[9] != opus->region.control->channels) {
        return false;
    }
    uint32_t gain = little16(head + 16);
    opus->pre_skip = little16(head + 10);
    return opus_decode_init(&opus->decoder, opus->state, sizeof opus->state, opus->work->scratch,
                            sizeof opus->work->scratch, OPUS_RATE, head[9]) == OPUS_DECODE_OK &&
           opus_decode_set_gain(&opus->decoder, (int32_t)gain - (gain >= 32768 ? 65536 : 0)) ==
               OPUS_DECODE_OK;
}

static bool set_origin(Core1Opus *opus, const OggRecord *record)
{
    if (record->flags != OGG_RECORD_ORIGIN || record->bytes != 0 || opus->audio_started ||
        opus->origin_set || record->granule > UINT32_MAX) {
        return false;
    }
    opus->origin = (uint32_t)record->granule;
    opus->origin_set = true;
    return true;
}

static bool anchor(Core1Opus *opus, uint64_t granule, uint32_t frames)
{
    if (granule != UINT64_MAX && granule < opus->origin) {
        return false;
    }
    if (opus->anchored || granule == UINT64_MAX) {
        return true;
    }
    uint64_t end = granule - opus->origin;
    if (end > UINT32_MAX || end < frames) {
        return false;
    }
    opus->next = (uint32_t)end - frames;
    opus->anchored = true;
    return true;
}

static bool stage(Core1Opus *opus, const OggRecord *record)
{
    OpusDecodePcm pcm = {.samples = opus->work->pcm, .capacity = OPUS_DECODE_MAX_FRAMES};
    OpusDecodeInput input = {.packet = opus->work->packet, .bytes = record->bytes};
    opus->audio_started = true;
    if ((record->flags & (OGG_RECORD_FIRST | OGG_RECORD_ORIGIN)) != 0 ||
        opus_decode_packet(&opus->decoder, input, &pcm) != OPUS_DECODE_OK ||
        !anchor(opus, record->granule, pcm.frames)) {
        return false;
    }
    opus->staged = opus->anchored ? pcm.frames : 0;
    opus->taken = 0;
    if (opus->staged == 0) {
        return true;
    }
    uint32_t start = opus->next;
    if (pcm.frames > UINT32_MAX - start) {
        return false;
    }
    opus->next = start + pcm.frames;
    uint32_t skip = opus->pre_skip > start ? opus->pre_skip - start : 0;
    opus->taken = core1_smaller(skip, pcm.frames);
    opus->first = start + opus->taken - opus->pre_skip;
    return true;
}

static bool accept(Core1Opus *opus, const OggRecord *record)
{
    if (!opus->decoder.ready) {
        return configure(opus, record);
    }
    if ((record->flags & OGG_RECORD_ORIGIN) != 0) {
        return set_origin(opus, record);
    }
    return record->bytes != 0 && stage(opus, record);
}

static Core1DecodeResult fetch(Core1Opus *opus, OggRecord *record)
{
    Core1DecodeResult result =
        core1_ogg_fetch(opus->input, opus->position, opus->work->packet, record);
    if (result != CORE1_DECODED) {
        return result;
    }
    if (opus->region.control->generation != opus->generation) {
        return CORE1_STARVED;
    }
    if ((record->flags & OGG_RECORD_END) != 0) {
        return record->bytes == 0 && record->flags == OGG_RECORD_END && opus->decoder.ready
                   ? CORE1_FINISHED
                   : CORE1_FAILED;
    }
    return CORE1_DECODED;
}

static void release(Core1Opus *opus, const OggRecord *record)
{
    opus->position += OGG_RECORD_BYTES + record->bytes;
    opus->region.control->released = opus->region.control->start_offset + opus->position;
    core1_publish();
}

static void publish(Core1Opus *opus, Core1Block *block)
{
    int32_t (*channels)[FLAC_MAX_BLOCK] = opus->region.samples[opus->slot];
    uint32_t width = opus->decoder.channels;
    uint32_t count = core1_smaller(opus->staged - opus->taken, FLAC_MAX_BLOCK);
    const int16_t *from = opus->work->pcm + opus->taken * width;
    for (uint32_t frame = 0; frame < count; frame++) {
        for (uint32_t channel = 0; channel < width; channel++) {
            channels[channel][frame] = (int32_t)from[frame * width + channel] * 256;
        }
    }
    *block = (Core1Block){.channels = {channels[0], channels[width - 1]},
                          .first_sample = opus->first,
                          .samples = count,
                          .position = opus->position};
    opus->first += count;
    opus->taken += count;
    opus->slot = (opus->slot + 1) % STREAM_SLOTS;
}

static Core1DecodeResult decode_opus(void *context, Core1Block *block)
{
    Core1Opus *opus = context;
    while (opus->taken == opus->staged) {
        OggRecord record;
        Core1DecodeResult result = fetch(opus, &record);
        if (result != CORE1_DECODED) {
            return result;
        }
        if (!accept(opus, &record)) {
            return CORE1_FAILED;
        }
        release(opus, &record);
    }
    publish(opus, block);
    return CORE1_DECODED;
}

Core1Decoder core1_opus_decoder(Core1Opus *opus, StreamRegion region)
{
    *opus = (Core1Opus){.region = region};
    return (Core1Decoder){start_opus, decode_opus, opus};
}
