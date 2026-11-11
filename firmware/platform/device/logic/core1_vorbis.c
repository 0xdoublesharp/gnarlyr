#include "device/core1_vorbis.h"

#include <stddef.h>

#include "device/core1_ogg.h"
#include "device/core1_source_private.h"

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
    vorbis->origin = 0;
    vorbis->origin_set = false;
    vorbis->region.control->vorbis_config_generation = 0;
    vorbis->audio_started = false;
    vorbis->positioned = false;
    vorbis->generation = vorbis->region.control->generation;
    vorbis->anchored = first_sample == 0;
    void *workspace = (void *)(uintptr_t)(vorbis->region.ring + vorbis->region.ring_bytes);
    vorbis->valid = vorbis_decoder_init(&vorbis->decoder, workspace, VORBIS_WORKSPACE_BYTES);
}

static Core1DecodeResult fetch_packet(Core1Vorbis *vorbis, VorbisPacket *packet)
{
    OggRecord record;
    Core1DecodeResult result =
        core1_ogg_fetch(vorbis->input, vorbis->position, vorbis->packet, &record);
    if (result != CORE1_DECODED) {
        return result;
    }
    if (vorbis->region.control->generation != vorbis->generation) {
        return CORE1_STARVED;
    }
    uint32_t flags = record.flags;
    if ((flags & OGG_RECORD_END) != 0) {
        return record.bytes == 0 && flags == OGG_RECORD_END && vorbis->decoder.ready
                   ? CORE1_FINISHED
                   : CORE1_FAILED;
    }
    vorbis->record_flags = flags;
    if ((flags & OGG_RECORD_ORIGIN) != 0) {
        if (flags != OGG_RECORD_ORIGIN || record.bytes != 0 || !vorbis->decoder.ready ||
            vorbis->audio_started || vorbis->origin_set || record.granule > UINT32_MAX) {
            return CORE1_FAILED;
        }
        *packet = (VorbisPacket){.granule = record.granule};
        return CORE1_DECODED;
    }
    if (record.bytes == 0) {
        return CORE1_FAILED;
    }
    *packet = (VorbisPacket){
        .span = {{vorbis->packet, record.bytes}, {NULL, 0}},
        .granule = record.granule,
        .first = (flags & OGG_RECORD_FIRST) != 0,
        .last = (flags & OGG_RECORD_LAST) != 0,
    };
    return CORE1_DECODED;
}

static void release_packet(Core1Vorbis *vorbis, const VorbisPacket *packet)
{
    vorbis->position += OGG_RECORD_BYTES + packet->span[0].size + packet->span[1].size;
    vorbis->region.control->released = vorbis->region.control->start_offset + vorbis->position;
    core1_publish();
}

static bool configure(Core1Vorbis *vorbis, const VorbisPacket *packet)
{
    if (!vorbis_decoder_header(&vorbis->decoder, *packet)) {
        return false;
    }
    if (vorbis->decoder.ready) {
        VorbisModes modes;
        if (!vorbis_decoder_modes(&vorbis->decoder, &modes)) {
            return false;
        }
        vorbis->region.control->vorbis_modes = modes;
        core1_publish();
        vorbis->region.control->vorbis_config_generation = vorbis->generation;
    }
    return true;
}

static bool decode_audio(Core1Vorbis *vorbis, const VorbisPacket *packet, Core1Block *block)
{
    int32_t (*channels)[FLAC_MAX_BLOCK] = vorbis->region.samples[vorbis->slot];
    VorbisPacket normalized = *packet;
    if (normalized.granule != UINT64_MAX) {
        if (normalized.granule < vorbis->origin) {
            return false;
        }
        normalized.granule -= vorbis->origin;
    }
    packet = &normalized;
    if (!vorbis->positioned) {
        if (vorbis->region.control->vorbis_start_generation == vorbis->generation) {
            core1_observe();
            vorbis->anchored = vorbis->region.control->first_sample == 0;
        }
        vorbis->positioned = true;
    }
    vorbis->audio_started = true;
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
        if (vorbis->record_flags == OGG_RECORD_ORIGIN) {
            vorbis->origin = (uint32_t)packet.granule;
            vorbis->origin_set = true;
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
