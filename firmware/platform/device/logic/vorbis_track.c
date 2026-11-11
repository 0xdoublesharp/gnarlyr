#include "device/vorbis_track.h"

#include <stddef.h>

#include "device/ogg_record.h"

static uint32_t little32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static bool header(const uint8_t *bytes, const OggPacket *packet, uint32_t index)
{
    static const uint8_t SIGNATURE[] = {'v', 'o', 'r', 'b', 'i', 's'};
    if (packet->bytes < 7 || packet->first != (index == 0) || packet->last ||
        bytes[0] != 1u + 2u * index) {
        return false;
    }
    for (uint32_t byte = 0; byte < sizeof SIGNATURE; byte++) {
        if (bytes[byte + 1] != SIGNATURE[byte]) {
            return false;
        }
    }
    return true;
}

static bool identification(const uint8_t *bytes, const OggPacket *packet, OggTrack *track)
{
    uint32_t small = bytes[28] & 15u;
    uint32_t large = bytes[28] >> 4;
    if (packet->bytes != 30 || little32(bytes + 7) != 0 || bytes[11] < 1 || bytes[11] > 2 ||
        small < 6 || large < small || large > 13 || bytes[29] != 1) {
        return false;
    }
    track->rate = little32(bytes + 12);
    track->channels = bytes[11];
    track->min_block = (uint16_t)(1u << small);
    track->max_block = (uint16_t)(1u << large);
    return track->rate >= 8000 && track->rate <= 192000;
}

static bool configuration(FlacInput input, OggTrack *track)
{
    OggPackets packets;
    ogg_packets_init(&packets, input);
    track->header_bytes = 3 * OGG_RECORD_BYTES;
    for (uint32_t index = 0; index < 3; index++) {
        uint8_t bytes[30] = {0};
        OggPacket packet;
        uint32_t count = index == 0 ? (uint32_t)sizeof bytes : 7u;
        if (ogg_packets_prefix(&packets, bytes, count, &packet) != OGG_PACKET_READY ||
            !header(bytes, &packet, index) ||
            (index == 0 && !identification(bytes, &packet, track)) ||
            (index == 1 && packet.bytes < 16) ||
            (index == 2 && (packet.bytes < 8 || packet.bytes > OGG_PACKET_BYTES))) {
            return false;
        }
        track->header_bytes += index == 1 ? 16 : packet.bytes;
    }
    track->serial = packets.serial;
    track->data_offset = packets.body;
    return true;
}

bool vorbis_track_probe(FlacInput input, OggTrack *track)
{
    OggTrack candidate = {.file_bytes = input.size};
    if (track == NULL || !configuration(input, &candidate) ||
        !ogg_track_endpoint(input, &candidate)) {
        return false;
    }
    *track = candidate;
    return true;
}

static uint32_t packet_block(uint8_t byte, const VorbisModes *modes)
{
    uint32_t bits = 0;
    for (uint32_t count = modes->count - 1; count != 0; count >>= 1) {
        bits++;
    }
    uint32_t mode = ((uint32_t)byte >> 1) & ((1u << bits) - 1);
    if ((byte & 1) != 0 || mode >= modes->count) {
        return 0;
    }
    bool long_block = ((modes->long_modes[mode / 32] >> (mode % 32)) & 1u) != 0;
    return long_block ? modes->long_block : modes->short_block;
}

bool vorbis_track_origin(const OggPackets *audio, const VorbisModes *modes, OggTrack *track)
{
    if (audio == NULL || modes == NULL || track == NULL || modes->count < 1 || modes->count > 64 ||
        modes->short_block != track->min_block || modes->long_block != track->max_block ||
        audio->serial != track->serial || audio->body != track->data_offset ||
        audio->input.size != track->file_bytes) {
        return false;
    }
    OggPackets cursor = *audio;
    uint32_t samples = 0;
    uint32_t previous = 0;
    for (;;) {
        OggPacket packet;
        uint8_t byte;
        if (ogg_packets_prefix(&cursor, &byte, 1, &packet) != OGG_PACKET_READY ||
            packet.bytes == 0 || packet.bytes > OGG_PACKET_BYTES) {
            return false;
        }
        uint32_t block = packet_block(byte, modes);
        uint32_t count = previous == 0 ? 0 : (previous + block) / 4;
        if (block == 0 || count > UINT32_MAX - samples) {
            return false;
        }
        samples += count;
        previous = block;
        if (packet.granule != UINT64_MAX) {
            if (packet.granule > track->end_granule) {
                return false;
            }
            uint32_t end = (uint32_t)packet.granule;
            uint32_t origin = end > samples ? end - samples : 0;
            if (origin >= track->end_granule) {
                return false;
            }
            track->origin = origin;
            track->samples = track->end_granule - origin;
            return true;
        }
    }
}
