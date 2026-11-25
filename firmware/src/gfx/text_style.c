#include <stddef.h>

#include "core/utf8.h"
#include "gfx/text.h"

int text_style_width(TextStyle style, const char *text)
{
    int digit_width = style.tabular ? text_digit_width(style.font) : 0;
    int width = 0;
    const char *cursor = text;
    for (uint32_t codepoint = utf8_next(&cursor); codepoint != 0; codepoint = utf8_next(&cursor)) {
        const Glyph *glyph = font_find_glyph(style.font, codepoint);
        if (glyph != NULL) {
            bool digit = codepoint >= '0' && codepoint <= '9';
            width += style.tabular && digit ? digit_width : glyph->advance;
        }
    }
    return width;
}
