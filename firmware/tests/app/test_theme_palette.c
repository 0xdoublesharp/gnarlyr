#include "check.h"
#include "core/hash.h"
#include "theme_palette.h"

static void a_palette_keeps_its_colors_and_decoration(void)
{
    ThemePalette palette = {.colors = {.background = COLOR_RGB(18, 16, 34),
                                       .text = COLOR_RGB(248, 242, 255),
                                       .accent = COLOR_RGB(124, 237, 228),
                                       .backdrop_bottom = COLOR_RGB(20, 14, 40)},
                            .horizon = true,
                            .header_rule = true};
    uint8_t bytes[THEME_PALETTE_BYTES];
    theme_palette_encode(&palette, bytes);
    ThemePalette decoded = {0};
    CHECK(theme_palette_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(palette.colors.background, decoded.colors.background);
    CHECK_EQUAL(palette.colors.text, decoded.colors.text);
    CHECK_EQUAL(palette.colors.accent, decoded.colors.accent);
    CHECK_EQUAL(palette.colors.backdrop_bottom, decoded.colors.backdrop_bottom);
    CHECK(decoded.horizon);
    CHECK(decoded.header_rule);
}

static void refused_data_leaves_the_fallback_untouched(void)
{
    ThemePalette palette = {.colors = {.text = 1234}};
    uint8_t bytes[THEME_PALETTE_BYTES];
    theme_palette_encode(&palette, bytes);
    ThemePalette fallback = {.colors = {.text = 4321}, .horizon = true};
    CHECK(!theme_palette_decode(bytes, sizeof bytes - 1, &fallback));
    bytes[8] ^= 1;
    CHECK(!theme_palette_decode(bytes, sizeof bytes, &fallback));
    CHECK_EQUAL(4321, fallback.colors.text);
    CHECK(fallback.horizon);
}

static void unknown_versions_flags_and_color_counts_are_refused(void)
{
    ThemePalette palette = {0};
    uint8_t bytes[THEME_PALETTE_BYTES];
    for (int field = 4; field <= 7; field++) {
        theme_palette_encode(&palette, bytes);
        bytes[field] = 255;
        hash_store(bytes + 48, hash_update(HASH_START, bytes, 48));
        CHECK(!theme_palette_decode(bytes, sizeof bytes, &palette));
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_palette_keeps_its_colors_and_decoration),
        TEST_CASE(refused_data_leaves_the_fallback_untouched),
        TEST_CASE(unknown_versions_flags_and_color_counts_are_refused),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
