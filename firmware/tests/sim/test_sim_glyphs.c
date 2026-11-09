#include <stddef.h>

#include "check.h"
#include "fonts/fonts.h"
#include "gfx/font_pack.h"
#include "sim_glyphs.h"

static const Font *const ALL_FONTS[] = {
    &FONT_INTER_REGULAR_16,
};

enum {
    FONT_COUNT = sizeof ALL_FONTS / sizeof ALL_FONTS[0]
};

static void the_committed_accent_packs_load(void)
{
    CHECK(sim_glyphs_face_count() >= 1);
}

static void every_accented_letter_comes_from_a_pack(void)
{
    for (size_t font = 0; font < FONT_COUNT; font++) {
        for (uint32_t codepoint = 0xC0; codepoint <= 0xFF; codepoint++) {
            const Glyph *glyph = font_find_glyph(ALL_FONTS[font], codepoint);
            CHECK(glyph != NULL && glyph->codepoint == codepoint);
            CHECK(glyph != NULL && font_pack_bitmap(glyph) != NULL);
        }
    }
}

static void the_22_px_fonts_have_cyrillic(void)
{
    static const Font *const fonts[] = {&FONT_INTER_REGULAR_22, &FONT_INTER_SEMIBOLD_22};
    static const uint32_t letters[] = {0x401, 0x404, 0x406, 0x407, 0x410, 0x416, 0x42F, 0x430,
                                       0x436, 0x44F, 0x451, 0x454, 0x456, 0x457, 0x490, 0x491};
    for (size_t font = 0; font < sizeof fonts / sizeof fonts[0]; font++) {
        for (size_t letter = 0; letter < sizeof letters / sizeof letters[0]; letter++) {
            const Glyph *glyph = font_find_glyph(fonts[font], letters[letter]);
            CHECK(glyph != NULL && glyph->codepoint == letters[letter] && glyph->width > 0);
        }
    }
}

static void the_no_break_space_is_blank(void)
{
    for (size_t font = 0; font < FONT_COUNT; font++) {
        const Glyph *glyph = font_find_glyph(ALL_FONTS[font], 0xA0);
        CHECK(glyph != NULL && glyph->codepoint == 0xA0 && glyph->width == 0 && glyph->advance > 0);
    }
}

int main(void)
{
    sim_glyphs_attach();
    static const TestCase cases[] = {
        TEST_CASE(the_committed_accent_packs_load),
        TEST_CASE(every_accented_letter_comes_from_a_pack),
        TEST_CASE(the_22_px_fonts_have_cyrillic),
        TEST_CASE(the_no_break_space_is_blank),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
