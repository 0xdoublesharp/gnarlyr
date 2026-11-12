#include "device/core1_ape.h"

#include <stddef.h>

#if !defined(__arm__)
#include <setjmp.h>
#include <string.h>
static jmp_buf checkpoint;
#else
static void *checkpoint[5];
#endif

const unsigned char *demac_input_end;

_Noreturn void demac_fail(void)
{
#if defined(__arm__)
    __builtin_longjmp(checkpoint, 1);
#else
    longjmp(checkpoint, 1);
#endif
}

static uint32_t little32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static void start_ape(void *context, FlacInput input, FlacInfo info, uint32_t first_sample,
                      const bool *starved)
{
    Core1Ape *ape = context;
    uint8_t *buffer = ape->buffer;
    StreamRegion region = ape->region;
    (void)info;
    (void)starved;
    *ape = (Core1Ape){.region = region,
                      .input = input,
                      .buffer = buffer,
                      .next_sample = first_sample,
                      .valid = true};
}

static Core1DecodeResult configure(Core1Ape *ape)
{
    uint8_t header[APE_CONFIG_BYTES];
    if (!ape->input.read(ape->input.context, 0, header, sizeof header)) {
        return CORE1_STARVED;
    }
    ApeTrack track = {.version = (uint16_t)little32(header),
                      .level = (uint16_t)little32(header + 4),
                      .bits = (uint16_t)little32(header + 8),
                      .channels = (uint16_t)little32(header + 12),
                      .rate = little32(header + 16)};
    if (!ape_track_allowed(&track) || track.version < 3970 || track.version > 3990) {
        return CORE1_FAILED;
    }
    core1_stream_clock(ape->region, ape_track_clock(&track) == APE_LIMIT_BOOST_MHZ
                                        ? APE_LIMIT_BOOST_MHZ
                                        : APE_LIMIT_BASE_MHZ);
    ape->decoder.fileversion = (int16_t)track.version;
    ape->decoder.compressiontype = track.level;
    ape->decoder.bps = track.bits;
    ape->decoder.channels = track.channels;
    ape->position = sizeof header;
    ape->configured = true;
    return CORE1_DECODED;
}

static Core1DecodeResult begin_frame(Core1Ape *ape)
{
    uint8_t header[APE_RECORD_BYTES];
    if (!ape->input.read(ape->input.context, ape->position, header, sizeof header)) {
        return CORE1_STARVED;
    }
    uint32_t bytes = little32(header);
    uint32_t samples = little32(header + 4);
    uint32_t skip = little32(header + 8);
    if (bytes == 0) {
        return CORE1_FINISHED;
    }
    if (samples == 0 || samples > 73728 * 16 || bytes < 8 || bytes % 4 != 0 || skip > 3 ||
        ape->position > ape->input.size - sizeof header ||
        bytes > ape->input.size - ape->position - sizeof header) {
        return CORE1_FAILED;
    }
    ape->position += sizeof header;
    ape->remaining = bytes;
    ape->buffered = 0;
    ape->consumed = 0;
    ape->first_byte = 3 - (int)skip;
    ape->decoder.currentframeblocks = (int)samples;
    ape->next_sample = little32(header + 12);
    ape->crc = ape_initcrc();
    return CORE1_DECODED;
}

static Core1DecodeResult fill(Core1Ape *ape)
{
    uint32_t keep = ape->buffered - ape->consumed;
    if (keep >= APE_INPUT_BYTES / 2 || ape->remaining == 0) {
        return CORE1_DECODED;
    }
    uint32_t count = APE_INPUT_BYTES - keep;
    count = count < ape->remaining ? count : ape->remaining;
    for (uint32_t index = 0; index < keep; index++) {
        ape->buffer[index] = ape->buffer[ape->consumed + index];
    }
    ape->buffered = keep;
    ape->consumed = 0;
    if (!ape->input.read(ape->input.context, ape->position, ape->buffer + keep, count)) {
        return CORE1_STARVED;
    }
    ape->buffered += count;
    ape->remaining -= count;
    ape->position += count;
    return CORE1_DECODED;
}

static void update_crc(Core1Ape *ape, uint32_t samples)
{
    uint8_t bytes[APE_CHUNK_SAMPLES * 2 * 3];
    uint32_t count = 0;
    for (uint32_t frame = 0; frame < samples; frame++) {
        for (uint32_t channel = 0; channel < ape->decoder.channels; channel++) {
            uint32_t sample = (uint32_t)ape->pcm[channel][frame];
            for (uint32_t byte = 0; byte < ape->decoder.bps / 8u; byte++) {
                bytes[count++] = (uint8_t)(sample >> (byte * 8));
            }
        }
    }
    ape->crc = ape_updatecrc(bytes, (int)count, ape->crc);
}

static Core1DecodeResult chunk(Core1Ape *ape, Core1Block *block)
{
#if defined(__arm__)
    if (__builtin_setjmp(checkpoint) != 0) {
#else
    if (setjmp(checkpoint) != 0) {
#endif
        ape->valid = false;
        return CORE1_FAILED;
    }
    demac_input_end = ape->buffer + ape->buffered;
    int consumed = 0;
    if (!ape->frame_started) {
        init_frame_decoder(&ape->decoder, ape->buffer + ape->consumed, &ape->first_byte, &consumed);
        ape->consumed += (uint32_t)consumed;
        ape->frame_started = true;
    }
    uint32_t left = (uint32_t)(ape->decoder.currentframeblocks - ape->decoder.blocksdecoded);
    uint32_t count = left < APE_CHUNK_SAMPLES ? left : APE_CHUNK_SAMPLES;
    if (decode_chunk(&ape->decoder, ape->buffer + ape->consumed, &ape->first_byte, &consumed,
                     ape->pcm[0], ape->pcm[1], (int)count) < 0 ||
        consumed < 0 || (uint32_t)consumed > ape->buffered - ape->consumed) {
        return CORE1_FAILED;
    }
    ape->consumed += (uint32_t)consumed;
    update_crc(ape, count);
    if (count == left) {
        if (ape_finishcrc(ape->crc) != ape->decoder.CRC) {
            return CORE1_FAILED;
        }
        ape->position += ape->remaining;
        ape->remaining = 0;
        ape->frame_started = false;
        ape->decoder.currentframeblocks = 0;
    }
    *block = (Core1Block){.channels = {ape->pcm[0], ape->pcm[ape->decoder.channels > 1 ? 1 : 0]},
                          .first_sample = ape->next_sample,
                          .samples = count,
                          .position = ape->position};
    ape->next_sample += count;
    return CORE1_DECODED;
}

static Core1DecodeResult decode_ape(void *context, Core1Block *block)
{
    Core1Ape *ape = context;
    if (!ape->valid) {
        return CORE1_FAILED;
    }
    Core1DecodeResult result = ape->configured ? CORE1_DECODED : configure(ape);
    if (result == CORE1_DECODED && ape->decoder.currentframeblocks == 0) {
        result = begin_frame(ape);
    }
    if (result == CORE1_DECODED) {
        result = fill(ape);
    }
    return result == CORE1_DECODED ? chunk(ape, block) : result;
}

Core1Decoder core1_ape_decoder(Core1Ape *ape, uint8_t *buffer, StreamRegion region)
{
    *ape = (Core1Ape){.region = region, .buffer = buffer};
    return (Core1Decoder){start_ape, decode_ape, ape};
}
