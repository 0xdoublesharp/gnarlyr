#include "theme_palette.h"

#include "core/hash.h"

enum {
    COLOR_COUNT = 20,
    COLORS_OFFSET = 8,
    CHECKSUM_OFFSET = 48,
    FLAG_HORIZON = 1,
    FLAG_HEADER_RULE = 2
};

static bool header_valid(const uint8_t *bytes, uint32_t length)
{
    return length == THEME_PALETTE_BYTES && bytes[0] == 'D' && bytes[1] == 'S' && bytes[2] == 'T' &&
           bytes[3] == 'H' && bytes[4] == 1 && bytes[5] < 4 && bytes[6] == COLOR_COUNT &&
           bytes[7] == 0 &&
           hash_load(bytes + CHECKSUM_OFFSET) == hash_update(HASH_START, bytes, CHECKSUM_OFFSET);
}

bool theme_palette_decode(const uint8_t *bytes, uint32_t length, ThemePalette *palette)
{
    if (!header_valid(bytes, length)) {
        return false;
    }
    Color colors[COLOR_COUNT];
    for (int index = 0; index < COLOR_COUNT; index++) {
        int at = COLORS_OFFSET + 2 * index;
        colors[index] = (Color)((uint16_t)bytes[at] | (uint16_t)bytes[at + 1] << 8);
    }
    *palette = (ThemePalette){.colors = {.background = colors[0],
                                         .text = colors[1],
                                         .text_secondary = colors[2],
                                         .text_tertiary = colors[3],
                                         .selection_background = colors[4],
                                         .selection_top = colors[5],
                                         .selection_shine = colors[6],
                                         .selection_text = colors[7],
                                         .header_background = colors[8],
                                         .header_top = colors[9],
                                         .header_text = colors[10],
                                         .divider = colors[11],
                                         .scrollbar_thumb = colors[12],
                                         .danger = colors[13],
                                         .success = colors[14],
                                         .chevron = colors[15],
                                         .accent = colors[16],
                                         .accent_alt = colors[17],
                                         .backdrop_top = colors[18],
                                         .backdrop_bottom = colors[19]},
                              .horizon = (bytes[5] & FLAG_HORIZON) != 0,
                              .header_rule = (bytes[5] & FLAG_HEADER_RULE) != 0};
    return true;
}

void theme_palette_encode(const ThemePalette *palette, uint8_t *bytes)
{
    const ThemeColors *c = &palette->colors;
    const Color colors[COLOR_COUNT] = {c->background,
                                       c->text,
                                       c->text_secondary,
                                       c->text_tertiary,
                                       c->selection_background,
                                       c->selection_top,
                                       c->selection_shine,
                                       c->selection_text,
                                       c->header_background,
                                       c->header_top,
                                       c->header_text,
                                       c->divider,
                                       c->scrollbar_thumb,
                                       c->danger,
                                       c->success,
                                       c->chevron,
                                       c->accent,
                                       c->accent_alt,
                                       c->backdrop_top,
                                       c->backdrop_bottom};
    static const uint8_t magic[] = {'D', 'S', 'T', 'H'};
    for (int index = 0; index < THEME_PALETTE_BYTES; index++) {
        bytes[index] = index < 4 ? magic[index] : 0;
    }
    bytes[4] = 1;
    bytes[5] = (uint8_t)((palette->horizon ? FLAG_HORIZON : 0) |
                         (palette->header_rule ? FLAG_HEADER_RULE : 0));
    bytes[6] = COLOR_COUNT;
    for (int index = 0; index < COLOR_COUNT; index++) {
        bytes[COLORS_OFFSET + 2 * index] = (uint8_t)colors[index];
        bytes[COLORS_OFFSET + 2 * index + 1] = (uint8_t)(colors[index] >> 8);
    }
    hash_store(bytes + CHECKSUM_OFFSET, hash_update(HASH_START, bytes, CHECKSUM_OFFSET));
}
