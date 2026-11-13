#include "ui/placeholder_art.h"

#include <stdbool.h>
#include <stddef.h>

#include "core/utf8.h"
#include "gfx/text.h"
#include "ui/icons.h"

enum {
    LAST_GLYPH_CODE_POINT = 0xFFFF,
    SYLLABIC_FIRST = 0x2E80,
    CASE_OFFSET = 0x20,
    CYRILLIC_EXTRA_OFFSET = 0x50,
    MIX_STEPS = 10,
    MIX_STRONGEST = 6,
    MIX_VARIETY = 2,
    SHADE_STEPS = 3,
    MIN_CONTRAST = 112,
    LUMA_RED = 77 * 8,
    LUMA_GREEN = 150 * 4,
    LUMA_BLUE = 29 * 8,
    LUMA_SHIFT = 8,
    MAX_SCALE = 2,
    SPAN_WIDTH_PARTS = 4,
    SPAN_HEIGHT_PARTS = 2,
    TILE_PARTS = 5,
    ICON_PARTS = 2
};

typedef struct Span {
    int left;
    int width;
    int top;
    int bottom;
} Span;

typedef struct Fit {
    const Font *font;
    int scale;
    Span span;
} Fit;

static bool is_space(uint32_t code_point)
{
    return code_point == ' ' || (code_point >= 0x09 && code_point <= 0x0D) || code_point == 0xA0 ||
           code_point == 0x3000;
}

static bool is_separator(uint32_t code_point)
{
    return is_space(code_point) || code_point == '/' || code_point == '-' || code_point == '_' ||
           code_point == '&' || code_point == '+';
}

static bool is_mark(uint32_t code_point)
{
    uint32_t folded = code_point | CASE_OFFSET;
    if (code_point < 0x80) {
        return !(code_point >= '0' && code_point <= '9') && !(folded >= 'a' && folded <= 'z');
    }
    return code_point < 0xC0 || code_point == 0xD7 || code_point == 0xF7 ||
           (code_point >= 0x2000 && code_point < 0x2C00) ||
           (code_point >= 0x3000 && code_point < 0x3040) ||
           (code_point >= 0xFE00 && code_point < 0xFE70) ||
           (code_point >= 0xFF00 && code_point < 0xFF10) || code_point >= 0xFFF0;
}

static uint32_t extended_upper(uint32_t code_point)
{
    bool odd_upper = (code_point >= 0x139 && code_point <= 0x148) ||
                     (code_point >= 0x179 && code_point <= 0x17E);
    bool lower = odd_upper ? code_point % 2 == 0 : code_point % 2 == 1;
    bool paired = code_point < 0x130 || (code_point > 0x131 && code_point != 0x138 &&
                                         code_point != 0x149 && code_point < 0x17F);
    return paired && lower ? code_point - 1 : code_point;
}

static uint32_t upper(uint32_t code_point)
{
    bool ascii = code_point >= 'a' && code_point <= 'z';
    bool latin = code_point >= 0xE0 && code_point <= 0xFE && code_point != 0xF7;
    bool greek = code_point >= 0x3B1 && code_point <= 0x3C9 && code_point != 0x3C2;
    bool cyrillic = code_point >= 0x430 && code_point <= 0x44F;
    if (ascii || latin || greek || cyrillic) {
        return code_point - CASE_OFFSET;
    }
    if (code_point >= 0x450 && code_point <= 0x45F) {
        return code_point - CYRILLIC_EXTRA_OFFSET;
    }
    return code_point >= 0x100 && code_point <= 0x17F ? extended_upper(code_point) : code_point;
}

static const char *skip_separators(const char *text)
{
    const char *cursor = text;
    while (true) {
        const char *before = cursor;
        uint32_t code_point = utf8_next(&cursor);
        if (code_point == 0 || !is_separator(code_point)) {
            return before;
        }
    }
}

static const char *after_article(const char *text)
{
    const char *start = skip_separators(text);
    const char *cursor = start;
    bool article = (utf8_next(&cursor) | CASE_OFFSET) == 't' &&
                   (utf8_next(&cursor) | CASE_OFFSET) == 'h' &&
                   (utf8_next(&cursor) | CASE_OFFSET) == 'e' && is_space(utf8_next(&cursor));
    const char *rest = skip_separators(cursor);
    return article && *rest != '\0' ? rest : start;
}

static uint32_t word_initial(const char **text)
{
    const char *cursor = skip_separators(*text);
    uint32_t initial = 0;
    while (true) {
        const char *before = cursor;
        uint32_t code_point = utf8_next(&cursor);
        if (code_point == 0 || is_separator(code_point)) {
            *text = before;
            return initial;
        }
        initial = initial == 0 && !is_mark(code_point) ? code_point : initial;
    }
}

static bool drawable(const Font *font, uint32_t code_point)
{
    const Glyph *glyph = code_point != 0 && code_point <= LAST_GLYPH_CODE_POINT
                             ? font_find_glyph(font, code_point)
                             : NULL;
    return glyph != NULL && glyph->codepoint == code_point;
}

static void add_letter(ArtInitials *initials, const Font *font, uint32_t letter)
{
    uint32_t shown = drawable(font, upper(letter)) ? upper(letter) : letter;
    if (drawable(font, shown)) {
        initials->letters[initials->count++] = shown;
    }
}

ArtInitials placeholder_art_initials(const Font *font, const char *text)
{
    ArtInitials initials = {.count = 0};
    const char *cursor = after_article(text);
    while (*cursor != '\0' && initials.count < PLACEHOLDER_ART_MAX_INITIALS) {
        add_letter(&initials, font, word_initial(&cursor));
        if (initials.count == 1 && initials.letters[0] >= SYLLABIC_FIRST) {
            break;
        }
        cursor = skip_separators(cursor);
    }
    return initials;
}

static int luma(Color color)
{
    int red = (color >> 11) & 0x1F;
    int green = (color >> 5) & 0x3F;
    int blue = color & 0x1F;
    return (red * LUMA_RED + green * LUMA_GREEN + blue * LUMA_BLUE) >> LUMA_SHIFT;
}

static int contrast(Color first, Color second)
{
    int difference = luma(first) - luma(second);
    return difference < 0 ? -difference : difference;
}

static uint32_t mixed(uint32_t seed)
{
    seed ^= seed >> 16;
    seed *= UINT32_C(0x45D9F3B);
    return seed ^ (seed >> 16);
}

static Color hue_of(const ThemeColors *colors, uint32_t pick)
{
    const Color hues[] = {colors->accent,          colors->accent_alt, colors->selection_top,
                          colors->selection_shine, colors->success,    colors->danger,
                          colors->scrollbar_thumb};
    return hues[pick % (sizeof hues / sizeof hues[0])];
}

ArtInk placeholder_art_ink(const ThemeColors *colors, uint32_t seed)
{
    uint32_t pick = mixed(seed);
    Color hue = hue_of(colors, pick);
    int strength = MIX_STRONGEST - (int)((pick >> 8) % MIX_VARIETY);
    Color top = color_between(colors->background, hue, strength, MIX_STEPS);
    while (strength > 0 && contrast(top, colors->text) < MIN_CONTRAST) {
        strength--;
        top = color_between(colors->background, hue, strength, MIX_STEPS);
    }
    return (ArtInk){.top = top,
                    .bottom = color_between(top, colors->background, 1, SHADE_STEPS),
                    .text = colors->text};
}

static int max_of(int first, int second)
{
    return first > second ? first : second;
}

static Span span_of(const Font *font, const ArtInitials *initials)
{
    Span span = {0, 0, 0, 0};
    int pen = 0;
    for (int index = 0; index < initials->count; index++) {
        Glyph glyph = *font_find_glyph(font, initials->letters[index]);
        span.left = index == 0 ? glyph.left : span.left;
        span.width = pen + glyph.left + glyph.width - span.left;
        span.top = max_of(span.top, glyph.top);
        span.bottom = max_of(span.bottom, glyph.height - glyph.top);
        pen += glyph.advance;
    }
    return span;
}

static bool fits(Span span, int scale, int size)
{
    return span.width * scale * TILE_PARTS <= size * SPAN_WIDTH_PARTS &&
           (span.top + span.bottom) * scale * TILE_PARTS <= size * SPAN_HEIGHT_PARTS;
}

static Fit fit_of(const PlaceholderArt *art, int size)
{
    const Font *fonts[] = {art->fonts.large, art->fonts.small};
    for (int scale = MAX_SCALE; scale >= 1; scale--) {
        for (size_t index = 0; index < sizeof fonts / sizeof fonts[0]; index++) {
            Fit fit = {fonts[index], scale, span_of(fonts[index], &art->initials)};
            if (fits(fit.span, scale, size)) {
                return fit;
            }
        }
    }
    return (Fit){art->fonts.small, 1, span_of(art->fonts.small, &art->initials)};
}

static void draw_initials(Canvas *canvas, const PlaceholderArt *art)
{
    Rect tile = canvas->bounds;
    Fit fit = fit_of(art, tile.width);
    int scale = fit.scale;
    int pen = tile.x + (tile.width - fit.span.width * scale) / 2 - fit.span.left * scale;
    int height = (fit.span.top + fit.span.bottom) * scale;
    int baseline = tile.y + (tile.height - height) / 2 + fit.span.top * scale;
    TextStyle style = {fit.font, art->ink.text, false};
    for (int index = 0; index < art->initials.count; index++) {
        uint32_t letter = art->initials.letters[index];
        Glyph glyph = *font_find_glyph(fit.font, letter);
        Rect box = {pen + glyph.left * scale, baseline - glyph.top * scale, glyph.width * scale,
                    glyph.height * scale};
        text_draw_glyph_scaled(canvas, style, box, letter, scale);
        pen += glyph.advance * scale;
    }
}

static void draw_icon(Canvas *canvas, const ArtInk *ink)
{
    Rect tile = canvas->bounds;
    int size = tile.width / ICON_PARTS;
    Rect area = {tile.x + (tile.width - size) / 2, tile.y + (tile.height - size) / 2, size, size};
    icon_draw(canvas, ICON_ALBUM, area, (IconInk){ink->text, ink->text});
}

void placeholder_art_draw(Canvas *canvas, const PlaceholderArt *art)
{
    canvas_fill_gradient(canvas, canvas->bounds, art->ink.top, art->ink.bottom);
    if (art->initials.count == 0) {
        draw_icon(canvas, &art->ink);
        return;
    }
    draw_initials(canvas, art);
}
