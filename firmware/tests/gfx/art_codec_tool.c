#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/art_codec.h"

enum {
    MAX_PAYLOAD = 4 * 1024 * 1024,
    MAX_WIDTH = 4096,
    HEAD_BYTES = 4
};

typedef struct Payload {
    const uint8_t *bytes;
    uint32_t size;
    int width;
    int height;
    uint32_t table;
    uint32_t entries;
    uint8_t codec;
    uint8_t alpha;
} Payload;

static uint8_t data[MAX_PAYLOAD];
static Color line[MAX_WIDTH];
static ArtPalette palette;

static uint32_t get32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static Color under(int x, int y)
{
    uint32_t value = (uint32_t)(x * 37 + y * 101) * 2654435761u;
    return (Color)(value >> 16);
}

static int parse(Payload *payload)
{
    if (payload->size < HEAD_BYTES) {
        return 0;
    }
    payload->codec = payload->bytes[0];
    payload->alpha = payload->bytes[2];
    payload->entries = payload->codec == ART_CODEC_INDEXED ? payload->bytes[1] + 1u : 0;
    uint32_t nibbles = payload->codec == ART_CODEC_INDEXED ? (payload->alpha + 1u) / 2 : 0;
    payload->table = HEAD_BYTES + payload->entries * 2 + nibbles;
    if ((payload->codec != ART_CODEC_INDEXED && payload->codec != ART_CODEC_LOSSLESS) ||
        payload->table + 4u * ((uint32_t)payload->height + 1) > payload->size) {
        return 0;
    }
    for (uint32_t index = 0; index < payload->entries; index++) {
        palette.colors[index] = (Color)(payload->bytes[HEAD_BYTES + 2 * index] |
                                        payload->bytes[HEAD_BYTES + 2 * index + 1] << 8);
    }
    memcpy(palette.alpha, payload->bytes + HEAD_BYTES + payload->entries * 2, nibbles);
    palette.alpha_entries = payload->codec == ART_CODEC_INDEXED ? payload->alpha : 0;
    palette.entries = payload->entries;
    return 1;
}

static int decode_row(const Payload *payload, int y, int from, int to)
{
    uint32_t start = get32(payload->bytes + payload->table + 4u * (uint32_t)y);
    uint32_t end = get32(payload->bytes + payload->table + 4u * ((uint32_t)y + 1));
    if (start > end || end > payload->size) {
        return 0;
    }
    ArtRow row = {payload->bytes + start, payload->bytes + end, payload->width};
    for (int x = from; x < to; x++) {
        line[x - from] = under(x, y);
    }
    ArtSpan span = {line, from, to};
    bool checked = art_row_check(row, payload->codec, payload->entries, payload->alpha != 0);
    bool structure = art_row_check(row, payload->codec, ART_PALETTE_MAX, payload->alpha != 0);
    ArtRow decoded = row;
    bool done = payload->codec == ART_CODEC_INDEXED
                    ? art_row_indexed(&decoded, &palette, span)
                    : art_row_lossless(&decoded, payload->alpha != 0, span);
    if (structure != (done && decoded.at == row.end)) {
        fprintf(stderr, "row %d: the check and the decoder disagree\n", y);
        exit(3);
    }
    return checked;
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: art_codec_tool PAYLOAD WIDTH HEIGHT FROM TO\n");
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) {
        return 2;
    }
    Payload payload = {
        data, (uint32_t)fread(data, 1, sizeof data, file), atoi(argv[2]), atoi(argv[3]), 0, 0, 0,
        0};
    fclose(file);
    int from = atoi(argv[4]);
    int to = atoi(argv[5]);
    if (payload.width < 1 || payload.width > MAX_WIDTH || payload.height < 1 || from < 0 ||
        to > payload.width || from > to || !parse(&payload)) {
        return 1;
    }
    for (int y = 0; y < payload.height; y++) {
        if (!decode_row(&payload, y, from, to)) {
            return 1;
        }
        fwrite(line, sizeof line[0], (size_t)(to - from), stdout);
    }
    return 0;
}
