#include <stdio.h>
#include <stdlib.h>

#include "device/dsd_stream.h"

static StreamControl control;
static int32_t samples[2][2][FLAC_MAX_BLOCK];
static _Alignas(8) uint8_t memory[DSD_RING_BYTES + sizeof(DsdWorkspace)];
static Core1Dsd decoder;

static bool read_file(void *context, uint32_t offset, void *out, uint32_t count)
{
    FILE *file = context;
    return fseek(file, offset, SEEK_SET) == 0 && fread(out, 1, count, file) == count;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    FILE *transport = fopen(argv[2], "wb");
    FILE *pcm = fopen(argv[3], "wb");
    if (file == NULL || transport == NULL || pcm == NULL) {
        return 3;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    if (size < 0 || (unsigned long)size > UINT32_MAX) {
        return 4;
    }
    DsdStream stream;
    AudioTrackInfo info;
    if (!dsd_stream_start(&stream, (FlacInput){read_file, file, (uint32_t)size}, 0, &info)) {
        return 5;
    }
    FlacInput input = {dsd_stream_read, &stream, DSD_HEADER_BYTES + stream.bytes};
    uint8_t buffer[4096];
    for (uint32_t at = 0; at < input.size;) {
        uint32_t count = input.size - at < sizeof buffer ? input.size - at : sizeof buffer;
        if (!input.read(input.context, at, buffer, count) ||
            fwrite(buffer, 1, count, transport) != count) {
            return 6;
        }
        at += count;
    }
    control.generation = 1;
    StreamRegion region = {
        .control = &control, .samples = samples, .ring = memory, .ring_bytes = DSD_RING_BYTES};
    Core1Decoder core = core1_dsd_decoder(&decoder, region);
    core.start(core.context, input, info.stream, 0, NULL);
    Core1Block block;
    Core1DecodeResult result;
    uint32_t done = 0;
    while ((result = core.decode(core.context, &block)) == CORE1_DECODED) {
        if (block.first_sample != done) {
            return 7;
        }
        for (uint32_t at = 0; at < block.samples; at++) {
            for (uint32_t channel = 0; channel < info.stream.channels; channel++) {
                if (fwrite(block.channels[channel] + at, sizeof(int32_t), 1, pcm) != 1) {
                    return 8;
                }
            }
        }
        done += block.samples;
    }
    bool success = result == CORE1_FINISHED && done == info.stream.total_samples;
    success = fclose(file) == 0 && success;
    success = fclose(transport) == 0 && success;
    success = fclose(pcm) == 0 && success;
    return success ? 0 : 9;
}
