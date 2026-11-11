#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device/core1_vorbis.h"
#include "device/ogg_stream.h"

static OggStream stream;
static Core1Vorbis vorbis;
static StreamControl control;
static int32_t samples[STREAM_SLOTS][FLAC_MAX_CHANNELS][FLAC_MAX_BLOCK];
static _Alignas(8) uint8_t ring[VORBIS_RING_BYTES + VORBIS_WORKSPACE_BYTES];
static uint8_t packet[OGG_PACKET_BYTES];
static uint32_t peak;
static uint32_t setup_peak;
static uint32_t maximum_packet;
static uint32_t maximum_difference;

static bool read_file(void *context, uint32_t offset, void *out, uint32_t bytes)
{
    FILE *file = context;
    return fseek(file, (long)offset, SEEK_SET) == 0 && fread(out, 1, bytes, file) == bytes;
}

static bool normalize(FlacInput input, OggTrack *track)
{
    OggPackets reader;
    ogg_packets_init_first(&reader, input);
    VorbisDecoder decoder;
    if (!vorbis_decoder_init(&decoder, ring + VORBIS_RING_BYTES, VORBIS_WORKSPACE_BYTES)) {
        return false;
    }
    for (uint32_t index = 0; index < 3; index++) {
        OggPacket info;
        OggPacketResult result = index == 1
                                     ? ogg_packets_prefix(&reader, packet, 7, &info)
                                     : ogg_packets_next(&reader, packet, sizeof packet, &info);
        if (result != OGG_PACKET_READY) {
            return false;
        }
        maximum_packet = info.bytes > maximum_packet ? info.bytes : maximum_packet;
        if (index == 1) {
            static const uint8_t EMPTY[] = {3, 'v', 'o', 'r', 'b', 'i', 's', 0,
                                            0, 0,   0,   0,   0,   0,   0,   1};
            memcpy(packet, EMPTY, sizeof EMPTY);
            info.bytes = sizeof EMPTY;
        }
        VorbisPacket header = {.span = {{packet, info.bytes}, {NULL, 0}},
                               .granule = info.granule,
                               .first = info.first};
        bool accepted = vorbis_decoder_header(&decoder, header);
        peak = decoder.memory.peak;
        if (!accepted) {
            return false;
        }
    }
    setup_peak = peak;
    VorbisModes modes;
    if (!vorbis_decoder_modes(&decoder, &modes) || !vorbis_track_origin(&reader, &modes, track)) {
        return false;
    }
    OggPacket info;
    OggPacketResult result;
    while ((result = ogg_packets_prefix(&reader, NULL, 0, &info)) == OGG_PACKET_READY) {
        maximum_packet = info.bytes > maximum_packet ? info.bytes : maximum_packet;
    }
    return result == OGG_PACKET_END;
}

static bool compare_block(FILE *reference, const Core1Block *block, uint32_t from,
                          uint32_t channels)
{
    for (uint32_t frame = from; frame < block->samples; frame++) {
        for (uint32_t channel = 0; channel < channels; channel++) {
            uint8_t bytes[2];
            if (fread(bytes, 1, 2, reference) != 2) {
                return false;
            }
            int16_t expected = (int16_t)((uint32_t)bytes[0] | (uint32_t)bytes[1] << 8);
            int32_t difference = (block->channels[channel][frame] >> 8) - expected;
            uint32_t absolute = (uint32_t)(difference < 0 ? -difference : difference);
            maximum_difference = absolute > maximum_difference ? absolute : maximum_difference;
            if (absolute > 2) {
                return false;
            }
        }
    }
    return true;
}

static bool compare(FlacInput input, const OggTrack *track, FILE *reference, uint32_t target)
{
    if (!ogg_stream_seek(&stream, input, track, target) ||
        fseek(reference, (long)target * track->channels * 2, SEEK_SET) != 0) {
        return false;
    }
    control = (StreamControl){.generation = 1};
    StreamRegion region = {
        .control = &control, .samples = samples, .ring = ring, .ring_bytes = VORBIS_RING_BYTES};
    Core1Decoder decoder = core1_vorbis_decoder(&vorbis, region);
    FlacInput records = {ogg_stream_read, &stream, OGG_VIRTUAL_END};
    decoder.start(decoder.context, records, (FlacInfo){0}, stream.seek_offset == 0 ? 0 : target,
                  NULL);
    uint32_t next = target;
    for (uint32_t count = 0; count < 100000; count++) {
        Core1Block block;
        Core1DecodeResult result = decoder.decode(decoder.context, &block);
        peak = vorbis.decoder.memory.peak > peak ? vorbis.decoder.memory.peak : peak;
        if (result != CORE1_DECODED) {
            return result == CORE1_FINISHED && next == track->samples && fgetc(reference) == EOF;
        }
        if (block.first_sample + block.samples <= target) {
            continue;
        }
        uint32_t from = block.first_sample < target ? target - block.first_sample : 0;
        if (block.first_sample + from != next ||
            !compare_block(reference, &block, from, track->channels)) {
            return false;
        }
        next += block.samples - from;
    }
    return false;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    FILE *reference = fopen(argv[2], "rb");
    if (file == NULL || reference == NULL || fseek(file, 0, SEEK_END) != 0) {
        return 2;
    }
    long size = ftell(file);
    if (size <= 0 || (unsigned long)size > UINT32_MAX) {
        return 2;
    }
    FlacInput input = {read_file, file, (uint32_t)size};
    OggTrack track = {0};
    bool probed = vorbis_track_probe(input, &track);
    bool configured = probed && normalize(input, &track);
    bool passed = configured;
    uint32_t targets[] = {0,
                          1,
                          track.samples / 3,
                          track.samples * 2 / 3,
                          track.samples == 0 ? 0 : track.samples - 1,
                          track.samples};
    for (uint32_t index = 0; passed && index < sizeof targets / sizeof targets[0]; index++) {
        passed = compare(input, &track, reference, targets[index]);
    }
    printf("{\"probe\":%s,\"configured\":%s,\"passed\":%s,\"rate\":%u,\"frames\":%u,"
           "\"setup_peak\":%u,\"workspace_peak\":%u,\"max_packet\":%u,\"max_difference\":%u}\n",
           probed ? "true" : "false", configured ? "true" : "false", passed ? "true" : "false",
           track.rate, track.samples, setup_peak, peak, maximum_packet, maximum_difference);
    fclose(file);
    fclose(reference);
    return passed ? 0 : 1;
}
