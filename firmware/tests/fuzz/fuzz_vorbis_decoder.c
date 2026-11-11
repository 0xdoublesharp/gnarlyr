#include <stddef.h>
#include <stdint.h>

#include "codec/ogg_packet.h"
#include "device/vorbis_decoder.h"
#include "fuzz_source.h"

enum {
    PACKET_BYTES = 65536,
    PACKET_LIMIT = 32,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static VorbisPacket packet_of(const uint8_t *bytes, OggPacket packet, uint32_t split)
{
    if (split > packet.bytes) {
        split = packet.bytes;
    }
    return (VorbisPacket){.span = {{bytes, split}, {bytes + split, packet.bytes - split}},
                          .granule = packet.granule,
                          .first = packet.first,
                          .last = packet.last};
}

static void exercise_invalid_packets(const uint8_t *data, size_t size)
{
    static _Alignas(8) uint8_t workspace[VORBIS_WORKSPACE_BYTES];
    VorbisDecoder decoder;
    fuzz_require(vorbis_decoder_init(&decoder, workspace, sizeof workspace));
    VorbisPacket packet = {
        .span = {{data, (uint32_t)size}, {NULL, 1}}, .granule = UINT64_MAX, .first = true};
    fuzz_require(!vorbis_decoder_header(&decoder, packet));
    packet.span[0] = (VorbisSpan){NULL, 0};
    packet.span[1] = (VorbisSpan){NULL, 0};
    fuzz_require(!vorbis_decoder_header(&decoder, packet));
    packet.span[0] = (VorbisSpan){data, size > INT32_MAX ? INT32_MAX : (uint32_t)size};
    packet.span[1] = (VorbisSpan){NULL, 0};
    packet.granule = (uint64_t)INT64_MAX + 1u;
    fuzz_require(!vorbis_decoder_header(&decoder, packet));
    fuzz_require(!vorbis_decoder_modes(&decoder, NULL));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > UINT32_MAX) {
        return 0;
    }
    exercise_invalid_packets(data, size);
    static uint8_t packet_bytes[PACKET_BYTES];
    static _Alignas(8) uint8_t workspace[VORBIS_WORKSPACE_BYTES];
    static int32_t samples[VORBIS_CHANNELS][VORBIS_MAX_FRAMES];
    FuzzSource source = {.bytes = data, .size = (uint32_t)size};
    OggPackets reader;
    ogg_packets_init(&reader, (FlacInput){fuzz_source(&source).read, &source, (uint32_t)size});
    VorbisDecoder decoder;
    fuzz_require(vorbis_decoder_init(&decoder, workspace, sizeof workspace));
    for (uint32_t header = 0; header < 3; header++) {
        OggPacket metadata;
        if (ogg_packets_next(&reader, packet_bytes, sizeof packet_bytes, &metadata) !=
            OGG_PACKET_READY) {
            fuzz_require(!source.invalid_read);
            return 0;
        }
        uint8_t control = size == 0 ? 0 : data[header % size];
        uint32_t split = metadata.bytes == 0 ? 0 : control % (metadata.bytes + 1u);
        if (!vorbis_decoder_header(&decoder, packet_of(packet_bytes, metadata, split))) {
            fuzz_require(!source.invalid_read);
            return 0;
        }
    }
    fuzz_require(decoder.ready && decoder.channels >= 1 && decoder.channels <= VORBIS_CHANNELS);
    VorbisModes modes;
    fuzz_require(vorbis_decoder_modes(&decoder, &modes));
    fuzz_require(modes.count >= 1 && modes.count <= 64);
    for (int step = 0; step < PACKET_LIMIT && !decoder.ended; step++) {
        OggPacket metadata;
        if (ogg_packets_next(&reader, packet_bytes, sizeof packet_bytes, &metadata) !=
            OGG_PACKET_READY) {
            break;
        }
        uint32_t split = metadata.bytes / 2;
        VorbisPcm pcm = {.channels = {samples[0], samples[1]}, .capacity = VORBIS_MAX_FRAMES};
        if (!vorbis_decoder_decode(&decoder, packet_of(packet_bytes, metadata, split), &pcm)) {
            break;
        }
        fuzz_require(pcm.frames <= pcm.capacity);
    }
    fuzz_require(!source.invalid_read);
    return 0;
}
