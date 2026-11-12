#include "device/opus_track.h"

#include <stddef.h>

#include "device/ogg_record.h"

enum {
    HEAD_BYTES = 19,
    TAGS_PREFIX = 8,
    MIN_BLOCK = 120,
    MAX_BLOCK = 4608
};

static bool signed_as(const uint8_t *bytes, const char *signature)
{
    for (uint32_t index = 0; signature[index] != '\0'; index++) {
        if (bytes[index] != (uint8_t)signature[index]) {
            return false;
        }
    }
    return true;
}

uint32_t opus_track_frames(const uint8_t *toc, uint32_t bytes)
{
    if (toc == NULL || bytes == 0) {
        return 0;
    }
    uint32_t config = toc[0] >> 3;
    uint32_t size = config < 12   ? (config & 3) == 3 ? 2880u : 480u << (config & 3)
                    : config < 16 ? 480u << (config & 1)
                                  : 120u << (config & 3);
    uint32_t code = toc[0] & 3u;
    uint32_t count = code == 0 ? 1 : code < 3 ? 2 : bytes > 1 ? toc[1] & 63u : 0;
    return count != 0 && size * count <= OPUS_PACKET_FRAMES ? size * count : 0;
}

static bool head(const uint8_t *bytes, const OggPacket *packet, OggTrack *track)
{
    if (packet->bytes < HEAD_BYTES || packet->bytes > OGG_PACKET_BYTES || !packet->first ||
        packet->last || !signed_as(bytes, "OpusHead") || bytes[8] > 15 || bytes[9] < 1 ||
        bytes[9] > 2 || bytes[18] != 0) {
        return false;
    }
    track->channels = bytes[9];
    track->pre_skip = (uint16_t)(bytes[10] | bytes[11] << 8);
    track->header_bytes = OGG_RECORD_BYTES + packet->bytes;
    return true;
}

static bool headers(OggPackets *packets, OggTrack *track)
{
    uint8_t bytes[HEAD_BYTES];
    OggPacket packet;
    if (ogg_packets_prefix(packets, bytes, sizeof bytes, &packet) != OGG_PACKET_READY ||
        !head(bytes, &packet, track) ||
        ogg_packets_prefix(packets, bytes, TAGS_PREFIX, &packet) != OGG_PACKET_READY ||
        packet.bytes < TAGS_PREFIX || packet.first || !signed_as(bytes, "OpusTags")) {
        return false;
    }
    track->serial = packets->serial;
    track->data_offset = packets->body;
    return true;
}

static bool origin(const OggPackets *audio, OggTrack *track)
{
    OggPackets cursor = *audio;
    uint32_t samples = 0;
    for (;;) {
        uint8_t toc[2];
        OggPacket packet;
        if (ogg_packets_prefix(&cursor, toc, sizeof toc, &packet) != OGG_PACKET_READY ||
            packet.bytes == 0 || packet.bytes > OGG_PACKET_BYTES) {
            return false;
        }
        uint32_t frames = opus_track_frames(toc, packet.bytes);
        if (frames == 0 || frames > UINT32_MAX - samples) {
            return false;
        }
        samples += frames;
        if (packet.granule != UINT64_MAX) {
            bool ended = (cursor.flags & 4) != 0;
            if (packet.granule > track->end_granule || (packet.granule < samples && !ended)) {
                return false;
            }
            track->origin = packet.granule > samples ? (uint32_t)packet.granule - samples : 0;
            return true;
        }
    }
}

bool opus_track_probe(FlacInput input, OggTrack *track)
{
    OggTrack candidate = {.rate = OPUS_TRACK_RATE,
                          .file_bytes = input.size,
                          .min_block = MIN_BLOCK,
                          .max_block = MAX_BLOCK,
                          .opus = true};
    OggPackets packets;
    ogg_packets_init_first(&packets, input);
    if (track == NULL || !headers(&packets, &candidate) || !ogg_track_endpoint(input, &candidate) ||
        !origin(&packets, &candidate) ||
        candidate.end_granule - candidate.origin <= candidate.pre_skip) {
        return false;
    }
    candidate.samples = candidate.end_granule - candidate.origin - candidate.pre_skip;
    *track = candidate;
    return true;
}

bool opus_track_seek(FlacInput input, const OggTrack *track, uint32_t sample, uint32_t *offset)
{
    if (track == NULL || offset == NULL || sample > track->samples) {
        return false;
    }
    uint32_t decoded = sample + track->pre_skip;
    if (sample == 0 || decoded < OPUS_PRE_ROLL) {
        *offset = 0;
        return true;
    }
    return ogg_track_seek(input, track, decoded - OPUS_PRE_ROLL, offset);
}
