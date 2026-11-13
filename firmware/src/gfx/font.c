#include "gfx/font.h"

#include <stdbool.h>
#include <stddef.h>

#include "core/i18n.h"
#include "gfx/font_pack.h"

enum {
    LEVELS_PER_BYTE = 2,
    HIGH_NIBBLE_SHIFT = 4,
    NIBBLE_MASK = 0x0F,
    BASE_FACES = 7,
    CHINESE_FACES = 7,
    JAPANESE_FACES = 14
};

static const Glyph *search(const Font *font, uint32_t codepoint)
{
    int low = 0;
    int high = (int)font->glyph_count - 1;
    while (low <= high) {
        int middle = low + (high - low) / 2;
        uint32_t found = font->glyphs[middle].codepoint;
        if (found == codepoint) {
            return &font->glyphs[middle];
        }
        if (found < codepoint) {
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return NULL;
}

const Glyph *font_find_glyph(const Font *font, uint32_t codepoint)
{
    if (font->glyph_count != 0) {
        uint32_t index = codepoint - font->glyphs[0].codepoint;
        if (index < font->glyph_count && font->glyphs[index].codepoint == codepoint) {
            return &font->glyphs[index];
        }
    }
    const Glyph *glyph = search(font, codepoint);
    if (glyph == NULL && font->pack_face != 0) {
        uint32_t face = font->pack_face - 1u;
        glyph = font_pack_find(face, codepoint);
        if (glyph == NULL && face < BASE_FACES) {
            const char *language = i18n_code();
            bool japanese = language[0] == 'j' && language[1] == 'a' &&
                            (language[2] == '\0' || language[2] == '-');
            glyph = font_pack_find(face + (japanese ? JAPANESE_FACES : CHINESE_FACES), codepoint);
            if (glyph == NULL) {
                glyph =
                    font_pack_find(face + (japanese ? CHINESE_FACES : JAPANESE_FACES), codepoint);
            }
        }
    }
    return glyph != NULL ? glyph : search(font, FONT_FALLBACK_CODEPOINT);
}

const uint8_t *font_glyph_bitmap(const Font *font, const Glyph *glyph)
{
    const uint8_t *packed = font_pack_bitmap(glyph);
    return packed != NULL ? packed : font->bitmap + glyph->bitmap_offset;
}

uint8_t font_glyph_level(const Font *font, const Glyph *glyph, int x, int y)
{
    return font_bitmap_level(font_glyph_bitmap(font, glyph), glyph, x, y);
}

uint8_t font_bitmap_level(const uint8_t *bitmap, const Glyph *glyph, int x, int y)
{
    int index = y * glyph->width + x;
    uint8_t packed = bitmap[index / LEVELS_PER_BYTE];
    bool high_nibble = index % LEVELS_PER_BYTE == 0;
    return (uint8_t)(high_nibble ? packed >> HIGH_NIBBLE_SHIFT : packed & NIBBLE_MASK);
}

int font_line_height(const Font *font)
{
    return font->ascent + font->descent;
}
