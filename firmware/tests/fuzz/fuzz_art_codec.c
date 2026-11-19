#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "gfx/art_codec.h"

enum {
    HEADER_BYTES = 6,
    MAX_WIDTH = 480,
    GUARD = 8,
    CANARY = 0xA5A5
};

static Color pixels[GUARD + MAX_WIDTH + GUARD];
static ArtPalette palette;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void check_guards(void)
{
    for (int at = 0; at < GUARD; at++) {
        if (pixels[at] != CANARY || pixels[GUARD + MAX_WIDTH + at] != CANARY) {
            abort();
        }
    }
}

static void prepare(const uint8_t *data)
{
    for (int at = 0; at < GUARD + MAX_WIDTH + GUARD; at++) {
        pixels[at] = CANARY;
    }
    for (int index = 0; index < ART_PALETTE_MAX; index++) {
        palette.colors[index] = (Color)(index * 257);
        palette.alpha[index / 2] = (uint8_t)(data[5] ^ index);
    }
    palette.alpha_entries = data[4];
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < HEADER_BYTES) {
        return 0;
    }
    int width = (int)(((uint32_t)data[0] | (uint32_t)data[1] << 8) % MAX_WIDTH) + 1;
    int from = data[2] % (width + 1);
    int to = from + data[3] % (width - from + 1);
    prepare(data);
    ArtRow row = {data + HEADER_BYTES, data + size, width};
    ArtSpan span = {pixels + GUARD + from, from, to};
    bool done = (data[5] & 1) ? art_row_lossless(&row, (data[5] & 2) != 0, span)
                              : art_row_indexed(&row, &palette, span);
    if (done && (row.at < data + HEADER_BYTES || row.at > data + size)) {
        abort();
    }
    for (int at = GUARD; at < GUARD + from; at++) {
        if (pixels[at] != CANARY) {
            abort();
        }
    }
    for (int at = GUARD + to; at < GUARD + MAX_WIDTH; at++) {
        if (pixels[at] != CANARY) {
            abort();
        }
    }
    check_guards();
    (void)art_row_indices_below((ArtRow){data + HEADER_BYTES, data + size, width}, data[4]);
    return 0;
}
