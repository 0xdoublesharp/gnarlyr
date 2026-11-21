#pragma once

#include <stdint.h>

enum {
    TEST_GLYPH_WIDTH = 3,
    TEST_GLYPH_HEIGHT = 2,
    TEST_GLYPH_BYTES = (TEST_GLYPH_WIDTH * TEST_GLYPH_HEIGHT + 1) / 2,
    TEST_GLYPH_LEFT = 1,
    TEST_GLYPH_TOP = -2,
    TEST_GLYPH_ADVANCE = 9
};

typedef struct TestGlyphFace {
    uint32_t count;
    uint32_t first;
    uint32_t step;
} TestGlyphFace;

uint32_t test_glyph_face_codepoint(TestGlyphFace face, uint32_t glyph);
uint32_t test_glyph_face_index_sectors(TestGlyphFace face);
uint32_t test_glyph_face_roots(TestGlyphFace face);
uint32_t test_glyph_face_data_offset(TestGlyphFace face);
uint32_t test_glyph_face_size(TestGlyphFace face);
uint32_t test_glyph_face_build(uint8_t *out, TestGlyphFace face);
void test_glyph_face_seal_header(uint8_t *out);
void test_glyph_face_seal_sector(uint8_t *out, uint32_t sector);

typedef struct TestExtraFont {
    TestGlyphFace faces[7];
    const char *const *languages;
    uint32_t language_count;
} TestExtraFont;

uint32_t test_extra_font_build(uint8_t *out, uint32_t capacity, const TestExtraFont *font);
uint32_t test_extra_font_face_offset(const uint8_t *file, uint32_t face);
void test_extra_font_seal(uint8_t *out);
