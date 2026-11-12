#include "board_layout.h"

#include "cover_cache.h"
#include "gfx/text.h"

const BoardLayout BOARD_LAYOUT = {
    .home_art_width = 180,
    .now_playing =
        {
            [TEXT_SIZE_SMALL] =
                {
                    .cover_size = 140,
                    .cover_top = 42,
                    .cover_gap = 20,
                    .title_top = 43,
                    .artist_top = 76,
                    .album_top = 110,
                    .text_line_height = 24,
                    .format_top = 134,
                    .detail_top = 154,
                    .modes_top = 174,
                    .small_line_height = 18,
                    .progress_top = 196,
                    .progress_height = 3,
                    .times_top = 202,
                },
            [TEXT_SIZE_MEDIUM] =
                {
                    .cover_size = 140,
                    .cover_top = 42,
                    .cover_gap = 20,
                    .title_top = 39,
                    .artist_top = 72,
                    .album_top = 110,
                    .text_line_height = 28,
                    .format_top = 134,
                    .detail_top = 154,
                    .modes_top = 174,
                    .small_line_height = 18,
                    .progress_top = 196,
                    .progress_height = 3,
                    .times_top = 202,
                },
            [TEXT_SIZE_LARGE] =
                {
                    .cover_size = 140,
                    .cover_top = 42,
                    .cover_gap = 20,
                    .title_top = 35,
                    .artist_top = 72,
                    .album_top = 110,
                    .text_line_height = 35,
                    .format_top = 134,
                    .detail_top = 154,
                    .modes_top = 174,
                    .small_line_height = 18,
                    .progress_top = 196,
                    .progress_height = 3,
                    .times_top = 202,
                },
        },
    .now_playing_portrait =
        {
            [TEXT_SIZE_SMALL] =
                {
                    .cover_size = 160,
                    .cover_top = 64,
                    .title_top = 250,
                    .artist_top = 306,
                    .album_top = 346,
                    .text_line_height = 24,
                    .format_top = 367,
                    .detail_top = 389,
                    .modes_top = 411,
                    .small_line_height = 18,
                    .progress_top = 440,
                    .progress_height = 3,
                    .times_top = 449,
                },
            [TEXT_SIZE_MEDIUM] =
                {
                    .cover_size = 160,
                    .cover_top = 64,
                    .title_top = 244,
                    .artist_top = 306,
                    .album_top = 346,
                    .text_line_height = 28,
                    .format_top = 367,
                    .detail_top = 389,
                    .modes_top = 411,
                    .small_line_height = 18,
                    .progress_top = 440,
                    .progress_height = 3,
                    .times_top = 449,
                },
            [TEXT_SIZE_LARGE] =
                {
                    .cover_size = 160,
                    .cover_top = 64,
                    .title_top = 232,
                    .artist_top = 306,
                    .album_top = 346,
                    .text_line_height = 35,
                    .format_top = 367,
                    .detail_top = 389,
                    .modes_top = 411,
                    .small_line_height = 18,
                    .progress_top = 440,
                    .progress_height = 3,
                    .times_top = 449,
                },
        },
    .home_art_min_height = COVER_FULL_SIZE + 16,
};

int board_text_size(const Theme *theme)
{
    int size = theme->metrics.text_size;
    return size >= 0 && size < TEXT_SIZE_COUNT ? size : TEXT_SIZE_MEDIUM;
}

const NowPlayingLayout *board_now_playing_layout(const Theme *theme)
{
    const NowPlayingLayout *table =
        theme_is_portrait(theme) ? BOARD_LAYOUT.now_playing_portrait : BOARD_LAYOUT.now_playing;
    return &table[board_text_size(theme)];
}

const char *board_splash_text(const Theme *theme)
{
    static const char FULL_NAME[] = FIRMWARE_NAME;
    static const char SHORT_NAME[] = FIRMWARE_SHORT_NAME;
    bool fits = text_width(theme->fonts.title, FULL_NAME) <= theme->metrics.screen_width;
    return fits ? FULL_NAME : SHORT_NAME;
}
