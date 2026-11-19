#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx/color.h"

enum {
    ART_CODEC_INDEXED = 1,
    ART_CODEC_LOSSLESS = 2,
    ART_PALETTE_MAX = 256,
    ART_ROW_MAX_BYTES = 1024
};

typedef struct ArtRow {
    const uint8_t *at;
    const uint8_t *end;
    int width;
} ArtRow;

typedef struct ArtSpan {
    Color *out;
    int from;
    int to;
} ArtSpan;

typedef struct ArtPalette {
    Color colors[ART_PALETTE_MAX];
    uint8_t alpha[ART_PALETTE_MAX / 2];
    uint32_t alpha_entries;
} ArtPalette;

bool art_row_indexed(ArtRow *row, const ArtPalette *palette, ArtSpan span);
bool art_row_lossless(ArtRow *row, bool alpha, ArtSpan span);
bool art_row_check(ArtRow row, uint8_t codec, uint32_t entries, bool alpha);
