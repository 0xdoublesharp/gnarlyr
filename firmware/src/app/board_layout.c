#include "board_layout.h"

#include "cover_cache.h"
#include "gfx/text.h"

enum {
    SPLASH_MARK_WIDTH_SHARE = 3,
    SPLASH_MARK_HEIGHT_SHARE = 2,
    SPLASH_MARK_PARTS = 7
};

const BoardLayout BOARD_LAYOUT = {
    .home_art_width = 180,
    .home_art_min_height = COVER_FULL_SIZE + 16,
};

int board_text_size(const Theme *theme)
{
    int size = theme->metrics.text_size;
    return size >= 0 && size < TEXT_SIZE_COUNT ? size : TEXT_SIZE_MEDIUM;
}

const char *board_splash_text(const Theme *theme)
{
    static const char FULL_NAME[] = FIRMWARE_NAME;
    static const char SHORT_NAME[] = FIRMWARE_SHORT_NAME;
    bool fits = text_width(theme->fonts.title, FULL_NAME) <= theme->metrics.screen_width;
    return fits ? FULL_NAME : SHORT_NAME;
}

SplashStack board_splash_stack(const Theme *theme, Rect area)
{
    const ThemeMetrics *metrics = &theme->metrics;
    int wide = metrics->screen_width * SPLASH_MARK_WIDTH_SHARE / SPLASH_MARK_PARTS;
    int tall = metrics->screen_height * SPLASH_MARK_HEIGHT_SHARE / SPLASH_MARK_PARTS;
    int size = wide < tall ? wide : tall;
    int gap = metrics->padding / 2;
    int line = font_line_height(theme->fonts.title);
    int top = area.y + (area.height - size - gap - line) / 2;
    return (SplashStack){{area.x + (area.width - size) / 2, top, size, size},
                         {area.x, top + size + gap, area.width, line}};
}
