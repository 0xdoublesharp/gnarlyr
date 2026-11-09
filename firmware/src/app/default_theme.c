#include "default_theme.h"

#include "fonts/fonts.h"
#include "hal/display.h"
#include "settings/settings.h"

#define THEME_COLORS                                                                               \
    {                                                                                              \
        .background = COLOR_RGB(18, 16, 34),                                                       \
        .text = COLOR_RGB(248, 242, 255),                                                          \
        .text_secondary = COLOR_RGB(212, 197, 236),                                                \
        .text_tertiary = COLOR_RGB(183, 170, 207),                                                 \
        .selection_background = COLOR_RGB(228, 157, 218),                                          \
        .selection_top = COLOR_RGB(244, 185, 231),                                                 \
        .selection_shine = COLOR_RGB(124, 237, 228),                                               \
        .selection_text = COLOR_RGB(37, 20, 43),                                                   \
        .header_background = COLOR_RGB(28, 20, 49),                                                \
        .header_top = COLOR_RGB(53, 32, 75),                                                       \
        .header_text = COLOR_RGB(248, 242, 255),                                                   \
        .divider = COLOR_RGB(73, 55, 99),                                                          \
        .scrollbar_thumb = COLOR_RGB(172, 133, 211),                                               \
        .danger = COLOR_RGB(255, 158, 165),                                                        \
        .success = COLOR_RGB(142, 233, 182),                                                       \
        .chevron = COLOR_RGB(212, 197, 236),                                                       \
        .status_danger = COLOR_RGB(255, 158, 165),                                                 \
        .status_success = COLOR_RGB(142, 233, 182),                                                \
        .status_pending = COLOR_RGB(183, 170, 207),                                                \
    }

#define CLASSIC_COLORS                                                                             \
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
        .status_danger = COLOR_RGB(255, 133, 112),                                                 \
        .status_success = COLOR_RGB(129, 221, 156),                                                \
        .status_pending = COLOR_RGB(189, 178, 152),                                                \
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

static const ThemeColors PALETTES[APPEARANCE_THEME_COUNT] = {
    THEME_COLORS,
    {
        .background = COLOR_RGB(39, 18, 34),
        .text = COLOR_RGB(255, 241, 225),
        .text_secondary = COLOR_RGB(241, 201, 203),
        .text_tertiary = COLOR_RGB(208, 172, 185),
        .selection_background = COLOR_RGB(255, 176, 128),
        .selection_top = COLOR_RGB(255, 207, 155),
        .selection_shine = COLOR_RGB(255, 232, 186),
        .selection_text = COLOR_RGB(51, 24, 36),
        .header_background = COLOR_RGB(57, 24, 47),
        .header_top = COLOR_RGB(87, 34, 67),
        .header_text = COLOR_RGB(255, 241, 225),
        .divider = COLOR_RGB(105, 60, 83),
        .scrollbar_thumb = COLOR_RGB(239, 160, 163),
        .danger = COLOR_RGB(255, 163, 164),
        .success = COLOR_RGB(168, 232, 177),
        .chevron = COLOR_RGB(241, 201, 203),
        .status_danger = COLOR_RGB(255, 163, 164),
        .status_success = COLOR_RGB(168, 232, 177),
        .status_pending = COLOR_RGB(208, 172, 185),
    },
    {
        .background = COLOR_RGB(10, 30, 34),
        .text = COLOR_RGB(232, 252, 240),
        .text_secondary = COLOR_RGB(180, 221, 215),
        .text_tertiary = COLOR_RGB(156, 197, 194),
        .selection_background = COLOR_RGB(125, 219, 191),
        .selection_top = COLOR_RGB(167, 241, 216),
        .selection_shine = COLOR_RGB(220, 255, 231),
        .selection_text = COLOR_RGB(12, 41, 41),
        .header_background = COLOR_RGB(14, 43, 47),
        .header_top = COLOR_RGB(23, 63, 65),
        .header_text = COLOR_RGB(232, 252, 240),
        .divider = COLOR_RGB(48, 83, 83),
        .scrollbar_thumb = COLOR_RGB(110, 181, 167),
        .danger = COLOR_RGB(255, 167, 166),
        .success = COLOR_RGB(156, 238, 180),
        .chevron = COLOR_RGB(180, 221, 215),
        .status_danger = COLOR_RGB(255, 167, 166),
        .status_success = COLOR_RGB(156, 238, 180),
        .status_pending = COLOR_RGB(156, 197, 194),
    },
    CLASSIC_COLORS,
};

void echo_theme_apply(Theme *theme, uint8_t appearance)
{
    unsigned index = appearance < APPEARANCE_THEME_COUNT ? appearance : APPEARANCE_NEON_NOIR;
    theme->colors = PALETTES[index];
}
