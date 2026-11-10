#include "device/core1_vorbis.h"

#include <stddef.h>

#include "device/core1_source_private.h"

static uint32_t little32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static void start_vorbis(void *context, FlacInput input, FlacInfo info, uint32_t first_sample,
                         const bool *starved)
{
    Core1Vorbis *vorbis = context;
    (void)info;
    (void)starved;
    vorbis->input = input;
    vorbis->position = 0;
    vorbis->next_sample = 0;
    vorbis->slot = 0;
    vorbis->generation = vorbis->region.control->generation;
    vorbis->anchored = first_sample == 0;
    void *workspace = (void *)(uintptr_t)(vorbis->region.ring + vorbis->region.ring_bytes);
    vorbis->valid = vorbis_decoder_init(&vorbis->decoder, workspace, VORBIS_WORKSPACE_BYTES);
}

static Core1DecodeResult fetch_packet(Core1Vorbis *vorbis, VorbisPacket *packet)
{
    uint8_t header[VORBIS_RECORD_BYTES];
    if (vorbis->position > vorbis->input.size ||
        sizeof header > vorbis->input.size - vorbis->position) {
        return CORE1_FAILED;
    }
    if (!vorbis->input.read(vorbis->input.context, vorbis->position, header, sizeof header)) {
        return CORE1_STARVED;
    }
    uint32_t bytes = little32(header);
    uint32_t flags = little32(header + 4);
    if ((flags & ~(uint32_t)(VORBIS_RECORD_FIRST | VORBIS_RECORD_LAST | VORBIS_RECORD_END)) != 0 ||
        bytes > VORBIS_PACKET_BYTES ||
        bytes > vorbis->input.size - vorbis->position - sizeof header) {
        return CORE1_FAILED;
    }
    if ((flags & VORBIS_RECORD_END) != 0) {
        return bytes == 0 && flags == VORBIS_RECORD_END && vorbis->decoder.ready ? CORE1_FINISHED
                                                                                 : CORE1_FAILED;
    }
    uint32_t body = vorbis->position + sizeof header;
    if (bytes == 0) {
        return CORE1_FAILED;
    }
    if (!vorbis->input.read(vorbis->input.context, body, vorbis->packet, bytes) ||
        vorbis->region.control->generation != vorbis->generation) {
        return CORE1_STARVED;
    }
    *packet = (VorbisPacket){
        .span = {{vorbis->packet, bytes}, {NULL, 0}},
        .granule = (uint64_t)little32(header + 8) | (uint64_t)little32(header + 12) << 32,
        .first = (flags & VORBIS_RECORD_FIRST) != 0,
        .last = (flags & VORBIS_RECORD_LAST) != 0,
    };
    return CORE1_DECODED;
}

static void release_packet(Core1Vorbis *vorbis, const VorbisPacket *packet)
{
    vorbis->position += VORBIS_RECORD_BYTES + packet->span[0].size + packet->span[1].size;
    vorbis->region.control->released = vorbis->region.control->start_offset + vorbis->position;
    core1_publish();
}

static bool configure(Core1Vorbis *vorbis, const VorbisPacket *packet)
{
    return vorbis_decoder_header(&vorbis->decoder, *packet);
}

static bool decode_audio(Core1Vorbis *vorbis, const VorbisPacket *packet, Core1Block *block)
{
    int32_t (*channels)[FLAC_MAX_BLOCK] = vorbis->region.samples[vorbis->slot];
    VorbisPcm pcm = {.channels = {channels[0], channels[1]}, .capacity = FLAC_MAX_BLOCK};
    if (!vorbis_decoder_decode(&vorbis->decoder, *packet, &pcm)) {
        return false;
    }
    bool known = packet->granule != UINT64_MAX;
    if (known && (packet->granule > UINT32_MAX || packet->granule < pcm.frames)) {
        return false;
    }
    if (known) {
        vorbis->next_sample = (uint32_t)packet->granule - pcm.frames;
        vorbis->anchored = true;
    }
    uint32_t frames = vorbis->anchored ? pcm.frames : 0;
    if (frames > UINT32_MAX - vorbis->next_sample) {
        return false;
    }
    *block = (Core1Block){.channels = {channels[0], channels[vorbis->decoder.channels > 1 ? 1 : 0]},
                          .first_sample = vorbis->next_sample,
                          .samples = frames};
    vorbis->next_sample += frames;
    vorbis->slot = (vorbis->slot + 1) % STREAM_SLOTS;
    return true;
}

static Core1DecodeResult decode_vorbis(void *context, Core1Block *block)
{
    Core1Vorbis *vorbis = context;
    if (!vorbis->valid) {
        return CORE1_FAILED;
    }
    if (vorbis->decoder.ended) {
        return CORE1_FINISHED;
    }
    for (;;) {
        VorbisPacket packet;
        Core1DecodeResult result = fetch_packet(vorbis, &packet);
        if (result != CORE1_DECODED) {
            return result;
        }
        if (!vorbis->decoder.ready) {
            if (!configure(vorbis, &packet)) {
                return CORE1_FAILED;
            }
            release_packet(vorbis, &packet);
            continue;
        }
        if (!decode_audio(vorbis, &packet, block)) {
            return CORE1_FAILED;
        }
        release_packet(vorbis, &packet);
        block->position = vorbis->position;
        return CORE1_DECODED;
    }
}

Core1Decoder core1_vorbis_decoder(Core1Vorbis *vorbis, StreamRegion region)
{
    *vorbis = (Core1Vorbis){.region = region};
    return (Core1Decoder){start_vorbis, decode_vorbis, vorbis};
}
