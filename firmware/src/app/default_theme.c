#include "default_theme.h"

#include "hal/storage.h"
#include "theme_palette.h"

static const ThemePalette PALETTES[APPEARANCE_THEME_COUNT] = {
    {.colors = {.background = COLOR_RGB(16, 17, 20),
                .text = COLOR_RGB(242, 242, 245),
                .text_secondary = COLOR_RGB(176, 176, 184),
                .text_tertiary = COLOR_RGB(128, 128, 136),
                .selection_background = COLOR_RGB(34, 96, 208),
                .selection_top = COLOR_RGB(66, 128, 232),
                .selection_shine = COLOR_RGB(120, 170, 248),
                .selection_text = COLOR_RGB(255, 255, 255),
                .header_background = COLOR_RGB(22, 23, 27),
                .header_top = COLOR_RGB(46, 47, 54),
                .header_text = COLOR_RGB(242, 242, 245),
                .divider = COLOR_RGB(48, 49, 56),
                .scrollbar_thumb = COLOR_RGB(96, 97, 106),
                .danger = COLOR_RGB(235, 77, 61),
                .success = COLOR_RGB(52, 199, 89),
                .chevron = COLOR_RGB(128, 128, 136),
                .accent = COLOR_RGB(96, 156, 244),
                .accent_alt = COLOR_RGB(150, 154, 170),
                .backdrop_top = COLOR_RGB(26, 27, 32),
                .backdrop_bottom = COLOR_RGB(18, 19, 22)},
     .horizon = false,
     .header_rule = false},
    {.colors = {.background = COLOR_RGB(18, 16, 34),
                .text = COLOR_RGB(248, 242, 255),
                .text_secondary = COLOR_RGB(212, 197, 236),
                .text_tertiary = COLOR_RGB(183, 170, 207),
                .selection_background = COLOR_RGB(228, 157, 218),
                .selection_top = COLOR_RGB(244, 185, 231),
                .selection_shine = COLOR_RGB(124, 237, 228),
                .selection_text = COLOR_RGB(37, 20, 43),
                .header_background = COLOR_RGB(28, 20, 49),
                .header_top = COLOR_RGB(53, 32, 75),
                .header_text = COLOR_RGB(248, 242, 255),
                .divider = COLOR_RGB(73, 55, 99),
                .scrollbar_thumb = COLOR_RGB(172, 133, 211),
                .danger = COLOR_RGB(255, 158, 165),
                .success = COLOR_RGB(142, 233, 182),
                .chevron = COLOR_RGB(212, 197, 236),
                .accent = COLOR_RGB(124, 237, 228),
                .accent_alt = COLOR_RGB(244, 150, 214),
                .backdrop_top = COLOR_RGB(58, 34, 98),
                .backdrop_bottom = COLOR_RGB(20, 14, 40)},
     .horizon = true,
     .header_rule = true},
    {.colors = {.background = COLOR_RGB(39, 18, 34),
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
                .accent = COLOR_RGB(255, 170, 110),
                .accent_alt = COLOR_RGB(255, 120, 160),
                .backdrop_top = COLOR_RGB(96, 36, 70),
                .backdrop_bottom = COLOR_RGB(44, 20, 38)},
     .horizon = true,
     .header_rule = true},
    {.colors = {.background = COLOR_RGB(10, 30, 34),
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
                .accent = COLOR_RGB(120, 228, 196),
                .accent_alt = COLOR_RGB(143, 230, 255),
                .backdrop_top = COLOR_RGB(22, 66, 70),
                .backdrop_bottom = COLOR_RGB(12, 36, 40)},
     .horizon = false,
     .header_rule = false},
    {.colors = {.background = COLOR_RGB(236, 225, 199),
                .text = COLOR_RGB(33, 31, 27),
                .text_secondary = COLOR_RGB(90, 77, 57),
                .text_tertiary = COLOR_RGB(103, 88, 65),
                .selection_background = COLOR_RGB(190, 110, 30),
                .selection_top = COLOR_RGB(238, 182, 85),
                .selection_shine = COLOR_RGB(248, 210, 139),
                .selection_text = COLOR_RGB(33, 31, 27),
                .header_background = COLOR_RGB(36, 37, 34),
                .header_top = COLOR_RGB(67, 66, 57),
                .header_text = COLOR_RGB(246, 235, 208),
                .divider = COLOR_RGB(181, 165, 131),
                .scrollbar_thumb = COLOR_RGB(115, 95, 61),
                .danger = COLOR_RGB(149, 41, 35),
                .success = COLOR_RGB(46, 98, 59),
                .chevron = COLOR_RGB(94, 80, 58),
                .accent = COLOR_RGB(166, 88, 20),
                .accent_alt = COLOR_RGB(60, 98, 110),
                .backdrop_top = COLOR_RGB(226, 212, 180),
                .backdrop_bottom = COLOR_RGB(214, 198, 162)},
     .horizon = false,
     .header_rule = false}};

static const char *const PALETTE_PATHS[APPEARANCE_THEME_COUNT] = {
    "/" FIRMWARE_DATA_DIRECTORY "/themes/graphite.bin",
    "/" FIRMWARE_DATA_DIRECTORY "/themes/neon.bin",
    "/" FIRMWARE_DATA_DIRECTORY "/themes/sunset.bin",
    "/" FIRMWARE_DATA_DIRECTORY "/themes/glass.bin",
    "/" FIRMWARE_DATA_DIRECTORY "/themes/classic.bin",
};

static bool read_palette(AppearanceTheme appearance, ThemePalette *palette)
{
    uint8_t bytes[THEME_PALETTE_BYTES];
    return hal_internal_file_read(PALETTE_PATHS[appearance], bytes, sizeof bytes) &&
           theme_palette_decode(bytes, sizeof bytes, palette);
}

void default_theme_set_appearance(Theme *theme, AppearanceTheme appearance)
{
    if ((unsigned)appearance >= APPEARANCE_THEME_COUNT) {
        appearance = APPEARANCE_NEON_NOIR;
    }
    if (theme->appearance == appearance) {
        return;
    }
    ThemePalette palette = PALETTES[appearance];
    read_palette(appearance, &palette);
    theme->colors = palette.colors;
    theme->horizon = palette.horizon;
    theme->header_rule = palette.header_rule;
    theme->appearance = (uint8_t)appearance;
}

void default_theme_invalidate(Theme *theme)
{
    theme->appearance = APPEARANCE_THEME_COUNT;
}
