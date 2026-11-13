#include "board_layout.h"

#include "cover_cache.h"
#include "gfx/text.h"

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
