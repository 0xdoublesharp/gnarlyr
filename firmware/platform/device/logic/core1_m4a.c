#include "device/core1_m4a.h"

#include <stddef.h>

#include "device/core1_heap.h"
#include "device/mp4_demux.h"

static uint32_t little_endian(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static void start_m4a(void *context, FlacInput input, FlacInfo info, uint32_t first_sample,
                      const bool *starved)
{
    Core1M4a *m4a = context;
    (void)info;
    (void)starved;
    core1_heap_reset();
    m4a->source = input;
    m4a->position = 0;
    m4a->next_sample = first_sample;
    m4a->codec = 0;
    m4a->alac = NULL;
    m4a->aac = NULL;
    m4a->packet = NULL;
}

static Core1DecodeResult fetch(Core1M4a *m4a, uint8_t *destination, uint32_t capacity,
                               uint32_t *size)
{
    uint8_t header[M4A_RECORD_HEADER_BYTES];
    if (m4a->position > m4a->source.size || sizeof header > m4a->source.size - m4a->position) {
        return CORE1_FAILED;
    }
    if (!m4a->source.read(m4a->source.context, m4a->position, header, sizeof header)) {
        return CORE1_STARVED;
    }
    *size = little_endian(header);
    if (*size == 0) {
        return CORE1_FINISHED;
    }
    if (*size > capacity || *size > m4a->source.size - m4a->position - sizeof header) {
        return CORE1_FAILED;
    }
    if (!m4a->source.read(m4a->source.context, m4a->position + sizeof header, destination, *size)) {
        return CORE1_STARVED;
    }
    return CORE1_DECODED;
}

static bool start_alac(Core1M4a *m4a, const uint8_t *cookie, uint32_t size)
{
    AlacConfig config;
    if (!alac_parse_cookie(cookie, size, &config)) {
        return false;
    }
    m4a->alac = core1_heap_allocate(sizeof *m4a->alac);
    m4a->packet_capacity = config.frame_length * config.channels * ((config.bit_depth + 7u) / 8u) +
                           M4A_ALAC_PACKET_SLACK;
    m4a->packet = core1_heap_allocate(m4a->packet_capacity);
    if (m4a->alac == NULL || m4a->packet == NULL) {
        return false;
    }
    alac_start(m4a->alac, &config);
    return true;
}

static bool start_aac(Core1M4a *m4a, const uint8_t *asc, uint32_t size)
{
    m4a->packet_capacity = AAC_MAX_PACKET_BYTES;
    m4a->packet = core1_heap_allocate(m4a->packet_capacity);
    m4a->aac = core1_heap_allocate(sizeof *m4a->aac);
    return m4a->packet != NULL && m4a->aac != NULL && aac_decoder_start(m4a->aac, asc, size);
}

static Core1DecodeResult configure(Core1M4a *m4a)
{
    uint8_t record[M4A_CONFIG_RECORD_BYTES];
    uint32_t size;
    Core1DecodeResult fetched = fetch(m4a, record, sizeof record, &size);
    if (fetched != CORE1_DECODED) {
        return fetched == CORE1_FINISHED ? CORE1_FAILED : fetched;
    }
    m4a->position += M4A_RECORD_HEADER_BYTES + size;
    m4a->codec = record[0];
    bool started = m4a->codec == MP4_CODEC_AAC    ? start_aac(m4a, record + 1, size - 1)
                   : m4a->codec == MP4_CODEC_ALAC ? start_alac(m4a, record + 1, size - 1)
                                                  : false;
    return started ? CORE1_DECODED : CORE1_FAILED;
}

static bool decode_packet(Core1M4a *m4a, uint32_t size, Core1Block *block)
{
    if (m4a->codec == MP4_CODEC_AAC) {
        if (!aac_decoder_decode(m4a->aac, m4a->packet, size)) {
            return false;
        }
        block->channels[0] = m4a->aac->channel[0];
        block->channels[1] = m4a->aac->channel[m4a->aac->channels > 1 ? 1 : 0];
        block->samples = m4a->aac->samples;
        return true;
    }
    if (alac_decode_packet(m4a->alac, m4a->packet, size) != ALAC_OK) {
        return false;
    }
    block->channels[0] = alac_channel(m4a->alac, 0);
    block->channels[1] = alac_channel(m4a->alac, 1);
    block->samples = alac_samples(m4a->alac);
    return true;
}

static Core1DecodeResult decode_m4a(void *context, Core1Block *block)
{
    Core1M4a *m4a = context;
    if (m4a->codec == 0) {
        Core1DecodeResult configured = configure(m4a);
        if (configured != CORE1_DECODED) {
            return configured;
        }
    }
    uint32_t size;
    Core1DecodeResult fetched = fetch(m4a, m4a->packet, m4a->packet_capacity, &size);
    if (fetched != CORE1_DECODED) {
        return fetched;
    }
    if (!decode_packet(m4a, size, block)) {
        return CORE1_FAILED;
    }
    m4a->position += M4A_RECORD_HEADER_BYTES + size;
    block->first_sample = m4a->next_sample;
    block->position = m4a->position;
    m4a->next_sample += block->samples;
    return CORE1_DECODED;
}

Core1Decoder core1_m4a_decoder(Core1M4a *m4a)
{
    *m4a = (Core1M4a){0};
    return (Core1Decoder){start_m4a, decode_m4a, m4a};
}
