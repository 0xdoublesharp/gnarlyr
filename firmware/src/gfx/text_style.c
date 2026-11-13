#include <stddef.h>

#include "core/utf8.h"
#include "gfx/text.h"

int text_style_width(TextStyle style, const char *text)
{
    int width = 0;
    const char *cursor = text;
    for (uint32_t codepoint = utf8_next(&cursor); codepoint != 0; codepoint = utf8_next(&cursor)) {
        const Glyph *glyph = font_find_glyph(style.font, codepoint);
        width += glyph != NULL ? text_cell_width(style, codepoint, glyph->advance) : 0;
    }
    return width;
}
