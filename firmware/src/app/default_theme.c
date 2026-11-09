#include "default_theme.h"

#include "fonts/fonts.h"
#include "hal/display.h"

#define THEME_COLORS                                                                               \
    {                                                                                              \
        .background = COLOR_RGB(236, 225, 199),                                                    \
        .text = COLOR_RGB(33, 31, 27),                                                             \
        .text_secondary = COLOR_RGB(90, 77, 57),                                                   \
        .text_tertiary = COLOR_RGB(103, 88, 65),                                                   \
        .selection_background = COLOR_RGB(211, 139, 47),                                           \
        .selection_top = COLOR_RGB(238, 182, 85),                                                  \
        .selection_shine = COLOR_RGB(248, 210, 139),                                               \
        .selection_text = COLOR_RGB(33, 31, 27),                                                   \
        .header_background = COLOR_RGB(36, 37, 34),                                                \
        .header_top = COLOR_RGB(67, 66, 57),                                                       \
        .header_text = COLOR_RGB(246, 235, 208),                                                   \
        .divider = COLOR_RGB(181, 165, 131),                                                       \
        .scrollbar_thumb = COLOR_RGB(115, 95, 61),                                                 \
        .danger = COLOR_RGB(149, 41, 35),                                                          \
        .success = COLOR_RGB(46, 98, 59),                                                          \
        .chevron = COLOR_RGB(94, 80, 58),                                                          \
    }

const Theme ECHO_THEME = {
    .colors = THEME_COLORS,
    .fonts =
        {
            .title = &FONT_INTER_SEMIBOLD_22,
            .body = &FONT_INTER_REGULAR_22,
            .small = &FONT_INTER_REGULAR_16,
        },
    .metrics =
        {
            .screen_width = HAL_DISPLAY_WIDTH,
            .screen_height = HAL_DISPLAY_HEIGHT,
            .header_height = 30,
            .header_inset = 84,
            .row_height = 32,
            .padding = 12,
            .scrollbar_width = 4,
            .scrollbar_inset = 2,
            .detail_gap = 10,
            .status_icon_size = 14,
        },
    .marquee = {.pause_ms = 1200, .speed_px_per_s = 56, .gap_px = 40},
};

const Theme ECHO_NOTICE_THEME = {
    .colors = THEME_COLORS,
    .fonts =
        {
            .title = &FONT_INTER_REGULAR_16,
            .body = &FONT_INTER_REGULAR_16,
            .small = &FONT_INTER_REGULAR_16,
        },
    .metrics =
        {
            .screen_width = HAL_DISPLAY_WIDTH,
            .screen_height = HAL_DISPLAY_HEIGHT,
            .header_height = 30,
            .header_inset = 84,
            .row_height = 32,
            .padding = 12,
            .scrollbar_width = 4,
            .scrollbar_inset = 2,
            .detail_gap = 10,
            .status_icon_size = 14,
        },
    .marquee = {.pause_ms = 1200, .speed_px_per_s = 56, .gap_px = 40},
};
