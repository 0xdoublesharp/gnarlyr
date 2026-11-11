#include <stddef.h>

#include "check.h"
#include "core/i18n.h"
#include "fonts/fonts.h"
#include "gfx/font_pack.h"
#include "sim_glyphs.h"
#include "test_pack.h"

static const Font *const ALL_FONTS[] = {
    &FONT_INTER_REGULAR_16,
};

enum {
    FONT_COUNT = sizeof ALL_FONTS / sizeof ALL_FONTS[0]
};

static void the_memory_left_beside_the_language_pack_caches_64_glyphs(void)
{
    CHECK_EQUAL(64, sim_glyphs_attach());
}

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

static const Font *const TEXT_SIZE_FONTS[] = {
    &FONT_INTER_REGULAR_18,  &FONT_INTER_SEMIBOLD_18, &FONT_INTER_REGULAR_22,
    &FONT_INTER_SEMIBOLD_22, &FONT_INTER_REGULAR_26,  &FONT_INTER_SEMIBOLD_26,
};

enum {
    TEXT_SIZE_FONT_COUNT = sizeof TEXT_SIZE_FONTS / sizeof TEXT_SIZE_FONTS[0],
    LARGE_BITMAP_BYTES = 256
};

static bool face_has(const Font *font, uint32_t codepoint)
{
    const Glyph *glyph = font_pack_find(font->pack_face - 1u, codepoint);
    return glyph != NULL && glyph->codepoint == codepoint;
}

static void every_text_size_face_has_the_letters_of_the_22_px_face(void)
{
    static const uint32_t ranges[][2] = {
        {0x20, 0x7E}, {0xA0, 0xFF}, {0x400, 0x45F}, {0x490, 0x491}};
    for (size_t range = 0; range < sizeof ranges / sizeof ranges[0]; range++) {
        for (uint32_t codepoint = ranges[range][0]; codepoint <= ranges[range][1]; codepoint++) {
            bool expected = face_has(&FONT_INTER_REGULAR_22, codepoint);
            for (size_t font = 0; font < TEXT_SIZE_FONT_COUNT; font++) {
                CHECK(face_has(TEXT_SIZE_FONTS[font], codepoint) == expected);
            }
        }
    }
}

static void the_large_faces_load_glyphs_over_256_bytes(void)
{
    static const Font *const fonts[] = {&FONT_INTER_REGULAR_26, &FONT_INTER_SEMIBOLD_26};
    static const uint32_t letters[] = {'@', 0x429, 0x409};
    for (size_t font = 0; font < sizeof fonts / sizeof fonts[0]; font++) {
        for (size_t letter = 0; letter < sizeof letters / sizeof letters[0]; letter++) {
            const Glyph *glyph = font_pack_find(fonts[font]->pack_face - 1u, letters[letter]);
            CHECK(glyph != NULL && (glyph->width * glyph->height + 1) / 2 > LARGE_BITMAP_BYTES);
            CHECK(glyph != NULL && font_pack_bitmap(glyph) != NULL);
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

static void cjk_glyphs_use_the_selected_language_face(void)
{
    static uint8_t pack[TEST_PACK_CAPACITY];
    CHECK_EQUAL(64, sim_glyphs_attach());
    for (uint32_t face = 7; face < 21; face++) {
        const Glyph *glyph = font_pack_find(face, 0x4EBA);
        CHECK(glyph != NULL && glyph->codepoint == 0x4EBA);
        CHECK(glyph != NULL && font_pack_bitmap(glyph) != NULL);
    }
    const Glyph *last_chinese = font_pack_find(7, 0xFF1B);
    CHECK(last_chinese != NULL && last_chinese->codepoint == 0xFF1B);
    const Glyph *last_japanese = font_pack_find(14, 0xFF5E);
    CHECK(last_japanese != NULL && last_japanese->codepoint == 0xFF5E);
    uint32_t size = test_pack_build(pack, (TestPackLanguage){"ja", "Japanese", NULL});
    CHECK(i18n_attach(pack, size));
    const Glyph *japanese = font_find_glyph(&FONT_INTER_REGULAR_22, 0x4EBA);
    CHECK(japanese != NULL && japanese->codepoint == 0x4EBA);
    CHECK(japanese == font_pack_find(15, 0x4EBA));
    size = test_pack_build(pack, (TestPackLanguage){"zh-CN", "Chinese", NULL});
    CHECK(i18n_attach(pack, size));
    const Glyph *chinese = font_find_glyph(&FONT_INTER_REGULAR_22, 0x4EBA);
    CHECK(chinese != NULL && chinese->codepoint == 0x4EBA);
    CHECK(chinese == font_pack_find(8, 0x4EBA));
    i18n_detach();
}

int main(void)
{
    sim_glyphs_attach();
    static const TestCase cases[] = {
        TEST_CASE(the_memory_left_beside_the_language_pack_caches_64_glyphs),
        TEST_CASE(cjk_glyphs_use_the_selected_language_face),
        TEST_CASE(the_committed_accent_packs_load),
        TEST_CASE(every_accented_letter_comes_from_a_pack),
        TEST_CASE(the_22_px_fonts_have_cyrillic),
        TEST_CASE(every_text_size_face_has_the_letters_of_the_22_px_face),
        TEST_CASE(the_large_faces_load_glyphs_over_256_bytes),
        TEST_CASE(the_no_break_space_is_blank),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
