#include <stddef.h>

#include "check.h"
#include "tiny_font.h"

static void a_glyph_is_found_by_its_codepoint(void)
{
    const Glyph *glyph = font_find_glyph(&TINY_FONT, 'B');
    CHECK(glyph != NULL);
    CHECK_EQUAL('B', glyph != NULL ? glyph->codepoint : 0);
}

static void a_missing_codepoint_falls_back_to_the_question_mark(void)
{
    const Glyph *glyph = font_find_glyph(&TINY_FONT, 0x4E00);
    CHECK(glyph != NULL);
    CHECK_EQUAL(FONT_FALLBACK_CODEPOINT, glyph != NULL ? glyph->codepoint : 0);
}

static void a_font_without_a_fallback_finds_nothing_for_missing_codepoints(void)
{
    CHECK(font_find_glyph(&TINY_FONT_WITHOUT_FALLBACK, 'Z') == NULL);
    CHECK(font_find_glyph(&TINY_FONT_WITHOUT_FALLBACK, 'A') != NULL);
}

static void contiguous_glyphs_and_out_of_range_codepoints_keep_their_exact_identity(void)
{
    Glyph glyphs[95] = {0};
    for (uint32_t index = 0; index < 95; index++) {
        glyphs[index].codepoint = (uint16_t)(32 + index);
    }
    Font font = {.glyphs = glyphs, .glyph_count = 95};
    for (uint32_t codepoint = 0; codepoint < 256; codepoint++) {
        uint32_t expected = codepoint >= 32 && codepoint < 127 ? codepoint : '?';
        CHECK(font_find_glyph(&font, codepoint) == &glyphs[expected - 32]);
    }
    CHECK(font_find_glyph(&font, UINT32_MAX) == &glyphs['?' - 32]);
    CHECK(font_find_glyph(&font, 0x10020) == &glyphs['?' - 32]);
}

static void sparse_and_empty_fonts_do_not_assume_a_contiguous_range(void)
{
    static const Glyph glyphs[] = {
        {.codepoint = '?'}, {.codepoint = 'A'}, {.codepoint = 'C'}, {.codepoint = 0x4E00}};
    Font sparse = {.glyphs = glyphs, .glyph_count = 4};
    for (uint32_t index = 0; index < 4; index++) {
        CHECK(font_find_glyph(&sparse, glyphs[index].codepoint) == &glyphs[index]);
    }
    CHECK(font_find_glyph(&sparse, 'B') == &glyphs[0]);
    Font empty = {0};
    CHECK(font_find_glyph(&empty, 0) == NULL);
    CHECK(font_find_glyph(&empty, 'A') == NULL);
}

static void levels_are_read_row_by_row_high_nibble_first(void)
{
    const Glyph *letter_a = font_find_glyph(&TINY_FONT, 'A');
    CHECK_EQUAL(15, font_glyph_level(&TINY_FONT, letter_a, 0, 0));
    CHECK_EQUAL(0, font_glyph_level(&TINY_FONT, letter_a, 1, 0));
    CHECK_EQUAL(0, font_glyph_level(&TINY_FONT, letter_a, 0, 1));
    CHECK_EQUAL(15, font_glyph_level(&TINY_FONT, letter_a, 1, 1));
    const Glyph *letter_b = font_find_glyph(&TINY_FONT, 'B');
    CHECK_EQUAL(4, font_glyph_level(&TINY_FONT, letter_b, 0, 0));
    CHECK_EQUAL(15, font_glyph_level(&TINY_FONT, letter_b, 1, 0));
}

static void line_height_spans_ascent_and_descent(void)
{
    CHECK_EQUAL(3, font_line_height(&TINY_FONT));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_glyph_is_found_by_its_codepoint),
        TEST_CASE(a_missing_codepoint_falls_back_to_the_question_mark),
        TEST_CASE(a_font_without_a_fallback_finds_nothing_for_missing_codepoints),
        TEST_CASE(contiguous_glyphs_and_out_of_range_codepoints_keep_their_exact_identity),
        TEST_CASE(sparse_and_empty_fonts_do_not_assume_a_contiguous_range),
        TEST_CASE(levels_are_read_row_by_row_high_nibble_first),
        TEST_CASE(line_height_spans_ascent_and_descent),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
