#include "default_theme.h"

#include "fonts/fonts.h"
#include "hal/display.h"

enum {
    LANDSCAPE_HEADER_HEIGHT = 30,
    LANDSCAPE_HEADER_INSET = 84,
    PORTRAIT_STATUS_HEIGHT = 20
};

#define THEME_COLORS                                                                               \
    {                                                                                              \
        .background = COLOR_RGB(16, 17, 20),                                                       \
        .text = COLOR_RGB(242, 242, 245),                                                          \
        .text_secondary = COLOR_RGB(176, 176, 184),                                                \
        .text_tertiary = COLOR_RGB(128, 128, 136),                                                 \
        .selection_background = COLOR_RGB(34, 96, 208),                                            \
        .selection_top = COLOR_RGB(82, 145, 240),                                                  \
        .selection_shine = COLOR_RGB(120, 170, 248),                                               \
        .selection_text = COLOR_RGB(255, 255, 255),                                                \
        .header_background = COLOR_RGB(22, 23, 27),                                                \
        .header_top = COLOR_RGB(46, 47, 54),                                                       \
        .header_text = COLOR_RGB(242, 242, 245),                                                   \
        .divider = COLOR_RGB(48, 49, 56),                                                          \
        .scrollbar_thumb = COLOR_RGB(96, 97, 106),                                                 \
        .danger = COLOR_RGB(235, 77, 61),                                                          \
        .success = COLOR_RGB(52, 199, 89),                                                         \
        .chevron = COLOR_RGB(128, 128, 136),                                                       \
        .accent = COLOR_RGB(96, 156, 244),                                                         \
        .accent_alt = COLOR_RGB(150, 154, 170),                                                    \
        .backdrop_top = COLOR_RGB(26, 27, 32),                                                     \
        .backdrop_bottom = COLOR_RGB(18, 19, 22),                                                  \
    }

const Theme ECHO_THEME = {
    .colors = THEME_COLORS,
    .appearance = APPEARANCE_THEME_COUNT,
    .fonts =
        {
            .title = &FONT_INTER_SEMIBOLD_22,
            .body = &FONT_INTER_REGULAR_22,
            .small = &FONT_INTER_REGULAR_16,
            .header = &FONT_INTER_SEMIBOLD_22,
        },
    .metrics =
        {
            .screen_width = HAL_DISPLAY_WIDTH,
            .screen_height = HAL_DISPLAY_HEIGHT,
            .header_height = LANDSCAPE_HEADER_HEIGHT,
            .header_inset = LANDSCAPE_HEADER_INSET,
            .row_height = 32,
            .padding = 12,
            .scrollbar_width = 4,
            .scrollbar_inset = 2,
            .detail_gap = 10,
            .status_icon_size = 14,
            .text_size = TEXT_SIZE_MEDIUM,
        },
    .marquee = {.pause_ms = 1200, .speed_px_per_s = 56, .gap_px = 40},
};

const Theme ECHO_NOTICE_THEME = {
    .colors = THEME_COLORS,
    .appearance = APPEARANCE_THEME_COUNT,
    .fonts =
        {
            .title = &FONT_INTER_REGULAR_16,
            .body = &FONT_INTER_REGULAR_16,
            .small = &FONT_INTER_REGULAR_16,
            .header = &FONT_INTER_REGULAR_16,
        },
    .metrics =
        {
            .screen_width = HAL_DISPLAY_WIDTH,
            .screen_height = HAL_DISPLAY_HEIGHT,
            .header_height = LANDSCAPE_HEADER_HEIGHT,
            .header_inset = LANDSCAPE_HEADER_INSET,
            .row_height = 32,
            .padding = 12,
            .scrollbar_width = 4,
            .scrollbar_inset = 2,
            .detail_gap = 10,
            .status_icon_size = 14,
            .text_size = TEXT_SIZE_MEDIUM,
        },
    .marquee = {.pause_ms = 1200, .speed_px_per_s = 56, .gap_px = 40},
};

typedef struct ThemeAccent {
    Color background;
    Color top;
    Color shine;
} ThemeAccent;

static const ThemeAccent ACCENTS[ACCENT_COLOR_COUNT] = {
    {COLOR_RGB(34, 96, 208), COLOR_RGB(82, 145, 240), COLOR_RGB(120, 170, 248)},
    {COLOR_RGB(200, 40, 48), COLOR_RGB(236, 88, 92), COLOR_RGB(246, 130, 130)},
    {COLOR_RGB(214, 104, 20), COLOR_RGB(244, 150, 64), COLOR_RGB(250, 184, 112)},
    {COLOR_RGB(36, 150, 72), COLOR_RGB(82, 196, 112), COLOR_RGB(130, 220, 150)},
    {COLOR_RGB(16, 140, 150), COLOR_RGB(60, 186, 194), COLOR_RGB(110, 212, 218)},
    {COLOR_RGB(118, 60, 200), COLOR_RGB(160, 108, 236), COLOR_RGB(190, 150, 246)},
    {COLOR_RGB(206, 52, 128), COLOR_RGB(240, 104, 170), COLOR_RGB(248, 150, 200)},
    {COLOR_RGB(96, 98, 110), COLOR_RGB(136, 138, 150), COLOR_RGB(170, 172, 182)},
};

void default_theme_set_accent(Theme *theme, AccentColor accent)
{
    if (theme->appearance != APPEARANCE_GRAPHITE && theme->appearance != APPEARANCE_THEME_COUNT) {
        return;
    }
    const ThemeAccent *colors = &ACCENTS[(int)accent < ACCENT_COLOR_COUNT ? accent : ACCENT_BLUE];
    theme->colors.selection_background = colors->background;
    theme->colors.selection_top = colors->top;
    theme->colors.selection_shine = colors->shine;
    theme->colors.accent = colors->shine;
}

typedef struct ThemeTextSize {
    const Font *title;
    const Font *body;
    int row_height;
} ThemeTextSize;

static const ThemeTextSize TEXT_SIZES[TEXT_SIZE_COUNT] = {
    {&FONT_INTER_SEMIBOLD_18, &FONT_INTER_REGULAR_18, 24},
    {&FONT_INTER_SEMIBOLD_22, &FONT_INTER_REGULAR_22, 32},
    {&FONT_INTER_SEMIBOLD_26, &FONT_INTER_REGULAR_26, 38},
};

void default_theme_set_text_size(Theme *theme, TextSize size)
{
    int index = (int)size < TEXT_SIZE_COUNT ? (int)size : TEXT_SIZE_MEDIUM;
    theme->fonts.title = TEXT_SIZES[index].title;
    theme->fonts.body = TEXT_SIZES[index].body;
    theme->metrics.row_height = TEXT_SIZES[index].row_height;
    theme->metrics.text_size = index;
}

void default_theme_set_orientation(Theme *theme, Orientation orientation)
{
    bool portrait = orientation != ORIENTATION_LANDSCAPE;
    ThemeMetrics *metrics = &theme->metrics;
    metrics->screen_width = portrait ? HAL_DISPLAY_SHORT_SIDE : HAL_DISPLAY_LONG_SIDE;
    metrics->screen_height = portrait ? HAL_DISPLAY_LONG_SIDE : HAL_DISPLAY_SHORT_SIDE;
    metrics->status_height = portrait ? PORTRAIT_STATUS_HEIGHT : 0;
    metrics->header_height = LANDSCAPE_HEADER_HEIGHT + metrics->status_height;
    metrics->header_inset = portrait ? metrics->padding : LANDSCAPE_HEADER_INSET;
}
