#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fonts/fonts.h"
#include "fuzz_source.h"
#include "ui/placeholder_art.h"

enum {
    TEXT_CAPACITY = 256,
    MAX_SIDE = 128,
    COLOR_BYTES = 16
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint16_t color_at(const uint8_t *data, size_t size, size_t offset)
{
    uint16_t first = offset < size ? data[offset] : 0;
    uint16_t second = offset + 1 < size ? data[offset + 1] : 0;
    return (uint16_t)(first | second << 8);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char text[TEXT_CAPACITY];
    size_t text_size = size < sizeof text - 1 ? size : sizeof text - 1;
    memcpy(text, data, text_size);
    text[text_size] = '\0';
    ThemeColors colors = {
        .background = color_at(data, size, 0),
        .text = color_at(data, size, 2),
        .selection_top = color_at(data, size, 4),
        .selection_shine = color_at(data, size, 6),
        .scrollbar_thumb = color_at(data, size, 8),
        .danger = color_at(data, size, 10),
        .success = color_at(data, size, 12),
        .accent = color_at(data, size, 14),
        .accent_alt = color_at(data, size, size > COLOR_BYTES ? size - 2 : 0),
    };
    uint32_t seed = 0;
    for (size_t index = 0; index < size; index++) {
        seed = seed * 33u + data[index];
    }
    ArtInitials initials = placeholder_art_initials(&FONT_INTER_SEMIBOLD_26, text);
    fuzz_require(initials.count >= 0 && initials.count <= PLACEHOLDER_ART_MAX_INITIALS);
    ArtInk ink = placeholder_art_ink(&colors, seed);
    PlaceholderArt art = {
        .initials = initials,
        .ink = ink,
        .fonts = {&FONT_INTER_SEMIBOLD_26, &FONT_INTER_SEMIBOLD_22},
    };
    static Color pixels[MAX_SIDE * MAX_SIDE];
    int side = size > 0 ? 1 + data[0] % MAX_SIDE : 1;
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, side, side});
    placeholder_art_draw(&canvas, &art);
    return 0;
}
