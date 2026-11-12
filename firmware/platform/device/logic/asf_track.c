#include "device/asf_track.h"

#include <stddef.h>

static uint32_t little32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static uint32_t little16(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8;
}

static uint64_t little64(const uint8_t *bytes)
{
    return (uint64_t)little32(bytes) | (uint64_t)little32(bytes + 4) << 32;
}

static bool guid(const uint8_t *bytes, uint32_t first, const uint8_t *tail)
{
    if (little32(bytes) != first) {
        return false;
    }
    for (uint32_t index = 0; index < 12; index++) {
        if (bytes[index + 4] != tail[index]) {
            return false;
        }
    }
    return true;
}

static const uint8_t HEADER_TAIL[] = {0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9,
                                      0,    0xAA, 0,    0x62, 0xCE, 0x6C};
static const uint8_t PROPERTY_TAIL[] = {0x47, 0xA9, 0xCF, 0x11, 0x8E, 0xE4,
                                        0,    0xC0, 0x0C, 0x20, 0x53, 0x65};
static const uint8_t STREAM_TAIL[] = {0xB7, 0xA9, 0xCF, 0x11, 0x8E, 0xE6,
                                      0,    0xC0, 0x0C, 0x20, 0x53, 0x65};
static const uint8_t AUDIO_TAIL[] = {0x4D, 0x5B, 0xCF, 0x11, 0xA8, 0xFD,
                                     0,    0x80, 0x5F, 0x5C, 0x44, 0x2B};

static bool properties(FlacInput input, uint32_t at, uint32_t size, AsfTrack *track)
{
    uint8_t bytes[80];
    if (size < 104 || !input.read(input.context, at + 24, bytes, sizeof bytes)) {
        return false;
    }
    uint64_t play = little64(bytes + 40) / 10000;
    uint64_t preroll = little64(bytes + 56);
    uint64_t packets = little64(bytes + 32);
    if (play <= preroll || play - preroll > UINT32_MAX || packets == 0 || packets > UINT32_MAX ||
        little32(bytes + 64) != 2 || little32(bytes + 68) != little32(bytes + 72)) {
        return false;
    }
    track->duration_ms = (uint32_t)(play - preroll);
    track->preroll_ms = (uint32_t)preroll;
    track->packets = (uint32_t)packets;
    track->packet_bytes = little32(bytes + 72);
    return track->packet_bytes >= 16 && track->packet_bytes <= ASF_PACKET_BYTES;
}

static bool audio_stream(FlacInput input, uint32_t at, uint32_t size, AsfTrack *track)
{
    uint8_t bytes[78];
    if (size < 96 || !input.read(input.context, at + 24, bytes, 72)) {
        return false;
    }
    if (!guid(bytes, 0xF8699E40u, AUDIO_TAIL)) {
        return true;
    }
    uint32_t type_bytes = little32(bytes + 40);
    uint32_t flags = little16(bytes + 48);
    uint32_t extra = little16(bytes + 70);
    if (track->stream != 0 || type_bytes < 18 || type_bytes > size - 78 ||
        extra > type_bytes - 18 || (flags & 0x8000) != 0 || (flags & 0x7F) == 0) {
        return false;
    }
    track->codec = (uint16_t)little16(bytes + 54);
    track->channels = (uint16_t)little16(bytes + 56);
    track->rate = little32(bytes + 58);
    uint64_t bitrate = (uint64_t)little32(bytes + 62) * 8;
    track->block_align = (uint16_t)little16(bytes + 66);
    track->stream = (uint8_t)(flags & 0x7F);
    track->extra_bytes = (uint16_t)(extra < sizeof track->extra ? extra : sizeof track->extra);
    if (bitrate == 0 || bitrate > 768000 || extra < (track->codec == 0x160 ? 4u : 6u)) {
        return false;
    }
    track->bitrate = (uint32_t)bitrate;
    if (!input.read(input.context, at + 96, bytes + 72, track->extra_bytes)) {
        return false;
    }
    for (uint32_t index = 0; index < track->extra_bytes; index++) {
        track->extra[index] = bytes[72 + index];
    }
    return (track->codec == 0x160 || track->codec == 0x161) && track->channels >= 1 &&
           track->channels <= 2 && track->rate >= 8000 && track->rate <= 48000 &&
           track->block_align > 0 && track->block_align <= WMA_PACKET_BYTES;
}

static bool object(FlacInput input, uint32_t *offset, uint32_t limit, AsfTrack *track)
{
    uint8_t header[24];
    uint32_t at = *offset;
    if (at > limit || sizeof header > limit - at ||
        !input.read(input.context, at, header, sizeof header)) {
        return false;
    }
    uint64_t size = little64(header + 16);
    if (size < sizeof header || size > limit - at || little32(header) == 0x2211B3FBu ||
        little32(header) == 0x298AE614u) {
        return false;
    }
    *offset += (uint32_t)size;
    if (guid(header, 0x8CABDCA1u, PROPERTY_TAIL)) {
        return track->packets == 0 && properties(input, at, (uint32_t)size, track);
    }
    return !guid(header, 0xB7DC0791u, STREAM_TAIL) ||
           audio_stream(input, at, (uint32_t)size, track);
}

bool asf_track_probe(FlacInput input, AsfTrack *track)
{
    uint8_t header[50];
    AsfTrack candidate = {0};
    if (track == NULL || input.read == NULL || input.size < sizeof header ||
        !input.read(input.context, 0, header, 30) || !guid(header, 0x75B22630u, HEADER_TAIL)) {
        return false;
    }
    uint64_t size = little64(header + 16);
    uint32_t count = little32(header + 24);
    if (size < 30 || size > input.size || count > (size - 30) / 24) {
        return false;
    }
    uint32_t offset = 30;
    for (uint32_t index = 0; index < count; index++) {
        if (!object(input, &offset, (uint32_t)size, &candidate)) {
            return false;
        }
    }
    if (offset != size || candidate.stream == 0 || candidate.packets == 0 ||
        sizeof header > input.size - offset ||
        !input.read(input.context, offset, header, sizeof header) ||
        !guid(header, 0x75B22636u, HEADER_TAIL)) {
        return false;
    }
    uint64_t data_bytes = little64(header + 16);
    uint64_t packet_bytes = (uint64_t)candidate.packets * candidate.packet_bytes;
    if (data_bytes < sizeof header || data_bytes > input.size - offset ||
        packet_bytes > data_bytes - sizeof header || little64(header + 40) != candidate.packets) {
        return false;
    }
    candidate.data_offset = offset + sizeof header;
    candidate.data_end = candidate.data_offset + (uint32_t)packet_bytes;
    uint32_t frame =
        candidate.rate <= 16000                                                            ? 512u
        : candidate.rate <= 22050 || (candidate.codec == 0x160 && candidate.rate <= 32000) ? 1024u
                                                                                           : 2048u;
    uint64_t frames =
        ((uint64_t)candidate.duration_ms * candidate.rate + frame * 500u) / (frame * 1000u);
    if (frames < 2 || (frames - 1) * frame > UINT32_MAX) {
        return false;
    }
    candidate.frame_samples = frame;
    candidate.samples = (uint32_t)((frames - 1) * frame);
    *track = candidate;
    return true;
}
