#include "device/core1_wma.h"

#include <string.h>

#if !defined(__arm__)
#include <setjmp.h>
static jmp_buf checkpoint;
#else
static void *checkpoint[5];
#endif

uint32_t wma_scratch[4096] WMA_WORKSPACE MEM_ALIGN_ATTR;

_Noreturn void wma_fail(void)
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

static void start_wma(void *context, FlacInput input, FlacInfo info, uint32_t first_sample,
                      const bool *starved)
{
    Core1Wma *wma = context;
    (void)info;
    (void)starved;
    wma->input = input;
    wma->position = 0;
    wma->next_sample = first_sample;
    wma->configured = false;
    wma->priming_frames = 0;
    wma->flushed = false;
    wma->valid = true;
    wma->generation = wma->region.control == NULL ? 0 : wma->region.control->generation;
    memset(wma->decoder, 0, sizeof *wma->decoder);
}

static Core1DecodeResult configure(Core1Wma *wma)
{
    uint8_t header[WMA_CONFIG_BYTES];
    if (!wma->input.read(wma->input.context, 0, header, sizeof header)) {
        return CORE1_STARVED;
    }
    asf_waveformatex_t config = {.codec_id = (uint16_t)little32(header),
                                 .channels = (uint16_t)little32(header + 4),
                                 .rate = little32(header + 8),
                                 .bitrate = little32(header + 12),
                                 .blockalign = (uint16_t)little32(header + 16),
                                 .datalen = (uint16_t)little32(header + 20)};
    if ((config.codec_id != 0x160 && config.codec_id != 0x161) || config.channels < 1 ||
        config.channels > 2 || config.rate < 8000 || config.rate > 48000 || config.bitrate == 0 ||
        config.bitrate > 768000 || config.blockalign == 0 || config.blockalign > WMA_PACKET_BYTES ||
        config.datalen < (config.codec_id == 0x160 ? 4u : 6u) || config.datalen > 6) {
        return CORE1_FAILED;
    }
    memcpy(config.data, header + 24, config.datalen);
    if (wma_decode_init(wma->decoder, &config) < 0) {
        return CORE1_FAILED;
    }
    memset(*wma->decoder->frame_out, 0, sizeof *wma->decoder->frame_out);
    wma->configured = true;
    wma->position = sizeof header;
    return CORE1_DECODED;
}

static Core1DecodeResult fetch(Core1Wma *wma)
{
    uint8_t header[4];
    if (wma->position > wma->input.size || sizeof header > wma->input.size - wma->position) {
        return CORE1_FAILED;
    }
    if (!wma->input.read(wma->input.context, wma->position, header, sizeof header)) {
        return CORE1_STARVED;
    }
    uint32_t bytes = little32(header);
    if (bytes == 0) {
        return CORE1_FINISHED;
    }
    if (bytes > WMA_PACKET_BYTES || bytes > wma->input.size - wma->position - sizeof header) {
        return CORE1_FAILED;
    }
    if (!wma->input.read(wma->input.context, wma->position + sizeof header, wma->packet, bytes)) {
        return CORE1_STARVED;
    }
    memset(wma->packet + bytes, 0, 8);
    wma->packet_bytes = bytes;
    wma->position += sizeof header + bytes;
    if (wma->region.control != NULL && wma->region.control->generation == wma->generation) {
        __atomic_thread_fence(__ATOMIC_RELEASE);
        wma->region.control->released = wma->position;
    }
    return wma_decode_superframe_init(wma->decoder, wma->packet, (int)bytes) > 0 ? CORE1_DECODED
                                                                                 : CORE1_FAILED;
}

static Core1DecodeResult output(Core1Wma *wma, Core1Block *block, uint32_t offset)
{
    WMADecodeContext *decoder = wma->decoder;
    for (uint32_t channel = 0; channel < (uint32_t)decoder->nb_channels; channel++) {
        for (uint32_t sample = 0; sample < (uint32_t)decoder->frame_len; sample++) {
            int32_t value = (*decoder->frame_out)[channel][offset + sample];
            (*decoder->frame_out)[channel][offset + sample] = (int32_t)(-(int64_t)value / 2);
        }
    }
    *block =
        (Core1Block){.channels = {(*decoder->frame_out)[0] + offset,
                                  (*decoder->frame_out)[decoder->nb_channels > 1 ? 1 : 0] + offset},
                     .first_sample = wma->next_sample,
                     .samples = (uint32_t)decoder->frame_len,
                     .position = wma->position};
    wma->next_sample += block->samples;
    return CORE1_DECODED;
}

static Core1DecodeResult decode_frame(Core1Wma *wma, Core1Block *block)
{
    WMADecodeContext *decoder = wma->decoder;
    for (;;) {
        if (decoder->current_frame >= decoder->nb_frames) {
            Core1DecodeResult result = fetch(wma);
            if (result == CORE1_FINISHED && !wma->flushed && wma->priming_frames == 2) {
                wma->flushed = true;
                return output(wma, block, (uint32_t)decoder->frame_len);
            }
            if (result != CORE1_DECODED) {
                return result;
            }
            if (decoder->nb_frames < 0 || decoder->nb_frames > 16) {
                return CORE1_FAILED;
            }
            if (decoder->nb_frames == 0) {
                continue;
            }
        }
        int count = wma_decode_superframe_frame(decoder, wma->packet, (int)wma->packet_bytes);
        if (count <= 0 || count > BLOCK_MAX_SIZE) {
            return CORE1_FAILED;
        }
        if (wma->priming_frames < 2) {
            wma->priming_frames++;
            continue;
        }
        return output(wma, block, 0);
    }
}

static Core1DecodeResult decode_wma(void *context, Core1Block *block)
{
    Core1Wma *wma = context;
    if (!wma->valid) {
        return CORE1_FAILED;
    }
#if defined(__arm__)
    if (__builtin_setjmp(checkpoint) != 0) {
#else
    if (setjmp(checkpoint) != 0) {
#endif
        wma->valid = false;
        return CORE1_FAILED;
    }
    Core1DecodeResult result = wma->configured ? CORE1_DECODED : configure(wma);
    return result == CORE1_DECODED ? decode_frame(wma, block) : result;
}

Core1Decoder core1_wma_decoder(Core1Wma *wma, WMADecodeContext *decoder, uint8_t *packet,
                               StreamRegion region)
{
    *wma = (Core1Wma){.decoder = decoder, .packet = packet, .region = region};
    return (Core1Decoder){start_wma, decode_wma, wma};
}
