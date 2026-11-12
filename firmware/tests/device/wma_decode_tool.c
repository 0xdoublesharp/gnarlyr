#include <stdio.h>
#include <stdlib.h>

#include "device/asf_stream.h"
#include "device/core1_wma.h"

static AsfStream adapter;
static Core1Wma wma;
static WMADecodeContext decode_context;
static uint8_t packet[WMA_PACKET_BYTES + 8];
static uint8_t records[1048576];

static bool read_file(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    FILE *file = context;
    return fseek(file, (long)offset, SEEK_SET) == 0 && fread(buffer, 1, length, file) == length;
}

static bool read_records(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const uint32_t *size = context;
    if (offset > *size || length > *size - offset) {
        return false;
    }
    memcpy(buffer, records + offset, length);
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 4) {
        return 2;
    }
    if (strcmp(argv[1], "--imdct") == 0) {
        int32_t input[128] = {1048576};
        int32_t output[256] = {0};
        ff_imdct_calc(8, output, input);
        return fwrite(output, sizeof output, 1, stdout) == 1 ? 0 : 9;
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return 3;
    }
    long length = ftell(file);
    AsfTrack track;
    uint32_t start_ms = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 10) : 0;
    if (length < 0 || (uint64_t)length > UINT32_MAX ||
        !asf_track_probe((FlacInput){read_file, file, (uint32_t)length}, &track) ||
        !asf_stream_start_at(&adapter, (FlacInput){read_file, file, (uint32_t)length}, &track,
                             start_ms)) {
        fclose(file);
        return 4;
    }
    uint32_t size = 0;
    while (!adapter.ended || adapter.record_position < 4) {
        if (size + 37 > sizeof records || !asf_stream_read(&adapter, size, records + size, 37)) {
            fprintf(stderr, "ASF failure at %u, packet %u, cursor %u, object %u/%u\n", size,
                    adapter.packet_index, adapter.cursor, adapter.object_bytes,
                    adapter.object_expected);
            fclose(file);
            return 5;
        }
        size += 37;
    }
    fclose(file);
    uint32_t target = (uint32_t)((uint64_t)start_ms * track.rate / 1000);
    if (argc == 4) {
        uint32_t info[] = {track.channels,       29,    track.rate, track.samples,
                           adapter.first_sample, target};
        if (fwrite(info, sizeof info, 1, stdout) != 1 || fwrite(records, size, 1, stdout) != 1) {
            return 6;
        }
        return 0;
    }
    Core1Decoder decoder = core1_wma_decoder(&wma, &decode_context, packet, (StreamRegion){0});
    bool starved = false;
    decoder.start(decoder.context, (FlacInput){read_records, &size, size}, (FlacInfo){0},
                  adapter.first_sample, &starved);
    Core1Block block;
    Core1DecodeResult result;
    while ((result = decoder.decode(decoder.context, &block)) == CORE1_DECODED) {
        for (uint32_t frame = 0; frame < block.samples; frame++) {
            if (block.first_sample + frame < target) {
                continue;
            }
            for (uint32_t channel = 0; channel < track.channels; channel++) {
                int32_t sample = block.channels[channel][frame];
                if (fwrite(&sample, sizeof sample, 1, stdout) != 1) {
                    return 7;
                }
            }
        }
    }
    if (result != CORE1_FINISHED) {
        fprintf(stderr, "decode failed: result %d at %u, samples %u, sizeof decode_context %zu\n",
                result, wma.position, wma.next_sample, sizeof decode_context);
        return 8;
    }
    fprintf(stderr, "decoded %u samples, metadata %u, context %zu\n", wma.next_sample,
            track.samples, sizeof decode_context);
    return 0;
}
