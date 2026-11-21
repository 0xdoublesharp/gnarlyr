#include <stddef.h>
#include <string.h>

#include "check.h"
#include "core/bytes.h"
#include "core/i18n.h"
#include "gfx/font_pack.h"
#include "test_glyph_face.h"
#include "test_pack.h"
#include "tiny_font.h"

enum {
    GLYPHS = 100,
    FIRST_CODEPOINT = 0x4E00,
    LARGE_GLYPHS = FONT_PACK_DIRECTORY_ENTRIES * FONT_PACK_ENTRIES_PER_SECTOR + 200,
    PACK_CAPACITY = 200000,
    MEMORY_BYTES = 16384
};

static const TestGlyphFace SMALL_FACE = {GLYPHS, FIRST_CODEPOINT, 2};
static const TestGlyphFace LARGE_FACE = {LARGE_GLYPHS, 0x1000, 1};

typedef struct FakePack {
    uint8_t bytes[PACK_CAPACITY];
    uint32_t size;
    uint32_t reads;
    bool failing;
} FakePack;

static FakePack pack;
static uint32_t memory[MEMORY_BYTES / 4];

static uint32_t codepoint_of(uint32_t glyph)
{
    return test_glyph_face_codepoint(SMALL_FACE, glyph);
}

static void build_face(TestGlyphFace face)
{
    pack.reads = 0;
    pack.failing = false;
    pack.size = test_glyph_face_build(pack.bytes, face);
}

static void build_pack(void)
{
    build_face(SMALL_FACE);
}

static bool read_pack(void *context, uint32_t face, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    pack.reads++;
    if (pack.failing || face != 0 || offset > pack.size || length > pack.size - offset) {
        return false;
    }
    memcpy(buffer, pack.bytes + offset, length);
    return true;
}

static const FontPackReader READER = {read_pack, NULL};
static Font PACKED_FONT;

static void attach(void)
{
    build_pack();
    font_pack_attach(READER, memory, sizeof memory);
    PACKED_FONT = TINY_FONT;
    PACKED_FONT.pack_face = 1;
}

static void a_packed_glyph_has_its_metrics(void)
{
    attach();
    const Glyph *glyph = font_pack_find(0, codepoint_of(5));
    CHECK(glyph != NULL);
    if (glyph == NULL) {
        return;
    }
    CHECK_EQUAL(codepoint_of(5), glyph->codepoint);
    CHECK_EQUAL(TEST_GLYPH_WIDTH, glyph->width);
    CHECK_EQUAL(TEST_GLYPH_HEIGHT, glyph->height);
    CHECK_EQUAL(TEST_GLYPH_LEFT, glyph->left);
    CHECK_EQUAL(TEST_GLYPH_TOP, glyph->top);
    CHECK_EQUAL(TEST_GLYPH_ADVANCE, glyph->advance);
}

static void a_packed_glyph_brings_its_bitmap(void)
{
    attach();
    const Glyph *glyph = font_pack_find(0, codepoint_of(7));
    const uint8_t *bitmap = glyph != NULL ? font_pack_bitmap(glyph) : NULL;
    CHECK(bitmap != NULL);
    if (bitmap == NULL) {
        return;
    }
    CHECK_EQUAL(7, font_bitmap_level(bitmap, glyph, 0, 0));
    CHECK_EQUAL(15, font_bitmap_level(bitmap, glyph, 1, 0));
    CHECK_EQUAL(0, font_bitmap_level(bitmap, glyph, 2, 1));
}

static void glyphs_in_later_index_sectors_are_found(void)
{
    attach();
    for (uint32_t glyph = 0; glyph < GLYPHS; glyph++) {
        const Glyph *found = font_pack_find(0, codepoint_of(glyph));
        CHECK(found != NULL && found->codepoint == codepoint_of(glyph));
    }
}

static void codepoints_outside_the_pack_are_missing(void)
{
    attach();
    CHECK(font_pack_find(0, codepoint_of(3) + 1) == NULL);
    CHECK(font_pack_find(0, FIRST_CODEPOINT - 1) == NULL);
    CHECK(font_pack_find(0, codepoint_of(GLYPHS - 1) + 2) == NULL);
    CHECK(font_pack_find(1, codepoint_of(0)) == NULL);
    CHECK(font_pack_find(FONT_PACK_FACES, codepoint_of(0)) == NULL);
    CHECK(font_pack_find(0, 0x1F600) == NULL);
}

static void a_second_lookup_reads_nothing(void)
{
    attach();
    font_pack_find(0, codepoint_of(10));
    font_pack_find(0, codepoint_of(10) + 1);
    uint32_t reads = pack.reads;
    CHECK(font_pack_find(0, codepoint_of(10)) != NULL);
    CHECK(font_pack_find(0, codepoint_of(10) + 1) == NULL);
    CHECK_EQUAL(reads, pack.reads);
}

static void the_header_is_read_once(void)
{
    attach();
    font_pack_find(0, codepoint_of(1));
    uint32_t reads = pack.reads;
    font_pack_find(0, codepoint_of(2));
    CHECK_EQUAL(reads + 3, pack.reads);
}

static void a_codepoint_outside_the_face_reads_nothing(void)
{
    attach();
    font_pack_find(0, codepoint_of(1));
    uint32_t reads = pack.reads;
    CHECK(font_pack_find(0, FIRST_CODEPOINT - 1) == NULL);
    CHECK(font_pack_find(0, codepoint_of(GLYPHS - 1) + 1) == NULL);
    CHECK(font_pack_find(0, 'A') == NULL);
    CHECK_EQUAL(reads, pack.reads);
}

static void glyphs_on_both_sides_of_a_directory_sector_are_found(void)
{
    build_face(LARGE_FACE);
    font_pack_attach(READER, memory, sizeof memory);
    CHECK_EQUAL(2, test_glyph_face_roots(LARGE_FACE));
    uint32_t boundary = FONT_PACK_DIRECTORY_ENTRIES * FONT_PACK_ENTRIES_PER_SECTOR;
    uint32_t glyphs[] = {0,
                         1,
                         boundary - 43,
                         boundary - 1,
                         boundary,
                         boundary + 41,
                         boundary + 42,
                         LARGE_GLYPHS - 1};
    for (uint32_t index = 0; index < sizeof glyphs / sizeof glyphs[0]; index++) {
        uint32_t codepoint = test_glyph_face_codepoint(LARGE_FACE, glyphs[index]);
        const Glyph *glyph = font_pack_find(0, codepoint);
        CHECK(glyph != NULL && glyph->codepoint == codepoint);
        const uint8_t *bitmap = glyph != NULL ? font_pack_bitmap(glyph) : NULL;
        CHECK(bitmap != NULL && font_bitmap_level(bitmap, glyph, 0, 0) == glyphs[index] % 16);
    }
    CHECK(font_pack_find(0, test_glyph_face_codepoint(LARGE_FACE, LARGE_GLYPHS)) == NULL);
}

static void a_face_holds_more_than_sixteen_index_sectors(void)
{
    TestGlyphFace face = {FONT_PACK_ENTRIES_PER_SECTOR * 40, 0x3000, 1};
    build_face(face);
    font_pack_attach(READER, memory, sizeof memory);
    for (uint32_t glyph = 0; glyph < face.count; glyph += 37) {
        uint32_t codepoint = test_glyph_face_codepoint(face, glyph);
        const Glyph *found = font_pack_find(0, codepoint);
        CHECK(found != NULL && found->codepoint == codepoint);
    }
}

static void forgetting_reads_the_pack_again(void)
{
    attach();
    font_pack_find(0, codepoint_of(4));
    uint32_t reads = pack.reads;
    font_pack_forget();
    CHECK(font_pack_find(0, codepoint_of(4)) != NULL);
    CHECK(pack.reads > reads);
}

static void a_pack_without_its_magic_is_ignored(void)
{
    attach();
    pack.bytes[0] = 'X';
    test_glyph_face_seal_header(pack.bytes);
    CHECK(font_pack_find(0, codepoint_of(0)) == NULL);
}

static void a_pack_of_another_version_is_ignored(void)
{
    attach();
    pack.bytes[4] = FONT_PACK_VERSION + 1;
    test_glyph_face_seal_header(pack.bytes);
    CHECK(font_pack_find(0, codepoint_of(0)) == NULL);
}

static void a_damaged_header_is_refused(void)
{
    attach();
    pack.bytes[8] ^= 1;
    CHECK(font_pack_find(0, codepoint_of(0)) == NULL);
    CHECK(font_pack_find(0, codepoint_of(50)) == NULL);
}

static void a_header_whose_glyph_count_disagrees_with_its_directory_is_refused(void)
{
    static const uint32_t counts[] = {0, 11000, FONT_PACK_MAX_GLYPHS + 1};
    for (uint32_t index = 0; index < sizeof counts / sizeof counts[0]; index++) {
        attach();
        bytes_put_le32(pack.bytes + 8, counts[index]);
        test_glyph_face_seal_header(pack.bytes);
        CHECK(font_pack_find(0, codepoint_of(0)) == NULL);
    }
}

static void a_header_that_puts_its_data_past_its_end_is_refused(void)
{
    attach();
    bytes_put_le32(pack.bytes + 12, test_glyph_face_data_offset(SMALL_FACE) - 1);
    test_glyph_face_seal_header(pack.bytes);
    CHECK(font_pack_find(0, codepoint_of(0)) == NULL);
}

static void a_damaged_directory_sector_finds_nothing_it_points_to(void)
{
    attach();
    pack.bytes[FONT_PACK_SECTOR_BYTES + 3] ^= 1;
    CHECK(font_pack_find(0, codepoint_of(0)) == NULL);
    CHECK(font_pack_find(0, codepoint_of(GLYPHS - 1)) == NULL);
}

static void a_damaged_index_sector_loses_only_its_own_glyphs(void)
{
    attach();
    uint32_t second = 1 + test_glyph_face_roots(SMALL_FACE) + 1;
    pack.bytes[second * FONT_PACK_SECTOR_BYTES + 5] ^= 1;
    CHECK(font_pack_find(0, codepoint_of(FONT_PACK_ENTRIES_PER_SECTOR)) == NULL);
    CHECK(font_pack_find(0, codepoint_of(FONT_PACK_ENTRIES_PER_SECTOR + 1)) == NULL);
    CHECK(font_pack_find(0, codepoint_of(0)) != NULL);
    CHECK(font_pack_find(0, codepoint_of(GLYPHS - 1)) != NULL);
}

static void a_sector_from_another_face_is_refused(void)
{
    static uint8_t other[PACK_CAPACITY];
    TestGlyphFace face = SMALL_FACE;
    face.first = FIRST_CODEPOINT + 1;
    test_glyph_face_build(other, face);
    attach();
    uint32_t index = (1 + test_glyph_face_roots(SMALL_FACE)) * FONT_PACK_SECTOR_BYTES;
    memcpy(pack.bytes + index, other + index, FONT_PACK_SECTOR_BYTES);
    CHECK(font_pack_find(0, codepoint_of(0)) == NULL);
    CHECK(font_pack_find(0, codepoint_of(0) + 1) == NULL);
}

static void a_bitmap_outside_the_data_is_refused(void)
{
    static const uint32_t offsets[] = {0, 2 * FONT_PACK_SECTOR_BYTES, 0xFFFFFFFFu};
    for (uint32_t index = 0; index < sizeof offsets / sizeof offsets[0]; index++) {
        attach();
        uint32_t sector = 1 + test_glyph_face_roots(SMALL_FACE);
        uint8_t *entry = pack.bytes + sector * FONT_PACK_SECTOR_BYTES;
        bytes_put_le32(entry + 8, offsets[index]);
        test_glyph_face_seal_sector(pack.bytes, sector);
        CHECK(font_pack_find(0, codepoint_of(0)) == NULL);
        CHECK(font_pack_find(0, codepoint_of(1)) != NULL);
    }
}

static void a_bitmap_running_past_the_end_is_refused(void)
{
    attach();
    uint32_t sector = 1 + test_glyph_face_roots(SMALL_FACE);
    uint8_t *entry = pack.bytes + sector * FONT_PACK_SECTOR_BYTES;
    bytes_put_le32(entry + 8, test_glyph_face_size(SMALL_FACE) - 1);
    test_glyph_face_seal_sector(pack.bytes, sector);
    CHECK(font_pack_find(0, codepoint_of(0)) == NULL);
}

static void a_truncated_face_finds_only_what_it_still_holds(void)
{
    uint32_t data = test_glyph_face_data_offset(SMALL_FACE);
    uint32_t ends[] = {FONT_PACK_HEADER_BYTES - 1, FONT_PACK_SECTOR_BYTES,
                       2 * FONT_PACK_SECTOR_BYTES + 100, data, data + 10 * TEST_GLYPH_BYTES};
    for (uint32_t index = 0; index < sizeof ends / sizeof ends[0]; index++) {
        attach();
        pack.size = ends[index];
        CHECK(font_pack_find(0, codepoint_of(GLYPHS - 1)) == NULL);
        CHECK((font_pack_find(0, codepoint_of(9)) != NULL) == (index == 4));
    }
}

static void a_failed_read_finds_nothing(void)
{
    attach();
    pack.failing = true;
    CHECK(font_pack_find(0, codepoint_of(0)) == NULL);
}

static void too_little_memory_keeps_packs_off(void)
{
    build_pack();
    CHECK_EQUAL(0, font_pack_attach(READER, memory, 64));
    CHECK(font_pack_find(0, codepoint_of(0)) == NULL);
}

static void the_memory_holds_whole_sets_of_slots(void)
{
    build_pack();
    uint32_t slots = font_pack_attach(READER, memory, sizeof memory);
    CHECK(slots >= FONT_PACK_WAYS);
    CHECK_EQUAL(0, slots % FONT_PACK_WAYS);
    uint32_t sets = slots / FONT_PACK_WAYS;
    CHECK_EQUAL(0, sets & (sets - 1));
}

static void the_compact_glyph_arena_keeps_the_same_slot_count(void)
{
    static uint32_t board_memory[(0x9000 - 1024) / 4];
    build_pack();
    CHECK_EQUAL(64, font_pack_attach(READER, board_memory, sizeof board_memory));
    CHECK_EQUAL(64, font_pack_attach(READER, board_memory, 0x6800 - 1024));
}

static void a_font_falls_back_to_its_pack(void)
{
    attach();
    const Glyph *glyph = font_find_glyph(&PACKED_FONT, codepoint_of(2));
    CHECK(glyph != NULL && glyph->codepoint == codepoint_of(2));
    if (glyph != NULL) {
        CHECK_EQUAL(2, font_glyph_level(&PACKED_FONT, glyph, 0, 0));
    }
    const Glyph *letter = font_find_glyph(&PACKED_FONT, 'A');
    CHECK(letter != NULL && letter->codepoint == 'A');
    if (letter != NULL) {
        CHECK_EQUAL(15, font_glyph_level(&PACKED_FONT, letter, 0, 0));
    }
}

static void a_character_in_neither_falls_back_to_the_question_mark(void)
{
    attach();
    const Glyph *glyph = font_find_glyph(&PACKED_FONT, codepoint_of(2) + 1);
    CHECK(glyph != NULL && glyph->codepoint == FONT_FALLBACK_CODEPOINT);
}

static void without_packs_a_font_uses_the_question_mark(void)
{
    attach();
    font_pack_detach();
    const Glyph *glyph = font_find_glyph(&PACKED_FONT, codepoint_of(2));
    CHECK(glyph != NULL && glyph->codepoint == FONT_FALLBACK_CODEPOINT);
}

static void a_font_reads_the_face_it_names(void)
{
    attach();
    PACKED_FONT.pack_face = 2;
    const Glyph *glyph = font_find_glyph(&PACKED_FONT, codepoint_of(2));
    CHECK(glyph != NULL && glyph->codepoint == FONT_FALLBACK_CODEPOINT);
}

static void a_font_without_a_pack_face_ignores_packs(void)
{
    attach();
    const Glyph *glyph = font_find_glyph(&TINY_FONT, codepoint_of(2));
    CHECK(glyph != NULL && glyph->codepoint == FONT_FALLBACK_CODEPOINT);
}

static bool read_cjk_fallback(void *context, uint32_t face, uint32_t offset, void *buffer,
                              uint32_t length)
{
    return face == 7 && read_pack(context, 0, offset, buffer, length);
}

static void a_missing_glyph_is_read_from_the_cjk_fallback_face(void)
{
    build_pack();
    font_pack_attach((FontPackReader){read_cjk_fallback, NULL}, memory, sizeof memory);
    PACKED_FONT = TINY_FONT;
    PACKED_FONT.pack_face = 1;
    const Glyph *glyph = font_find_glyph(&PACKED_FONT, codepoint_of(2));
    CHECK(glyph != NULL && glyph->codepoint == codepoint_of(2));
}

static uint32_t first_fallback_face;

static bool read_either_cjk_fallback(void *context, uint32_t face, uint32_t offset, void *buffer,
                                     uint32_t length)
{
    if (face >= 7 && first_fallback_face == UINT32_MAX) {
        first_fallback_face = face;
    }
    return (face == 7 || face == 14) && read_pack(context, 0, offset, buffer, length);
}

static void japanese_language_codes_try_the_japanese_face_before_chinese(void)
{
    static const struct {
        const char *code;
        uint32_t face;
    } cases[] = {{"en", 7}, {"jx", 7}, {"jax", 7}, {"ja", 14}, {"ja-JP", 14}};
    static uint8_t language[TEST_PACK_CAPACITY];
    for (uint32_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        uint32_t size =
            test_pack_build(language, (TestPackLanguage){cases[index].code, "Test language", NULL});
        CHECK(i18n_attach(language, size));
        build_pack();
        font_pack_attach((FontPackReader){read_either_cjk_fallback, NULL}, memory, sizeof memory);
        PACKED_FONT = TINY_FONT;
        PACKED_FONT.pack_face = 1;
        first_fallback_face = UINT32_MAX;
        const Glyph *glyph = font_find_glyph(&PACKED_FONT, codepoint_of(2));
        CHECK(glyph != NULL && glyph->codepoint == codepoint_of(2));
        CHECK_EQUAL(cases[index].face, first_fallback_face);
    }
    i18n_detach();
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_packed_glyph_has_its_metrics),
        TEST_CASE(a_packed_glyph_brings_its_bitmap),
        TEST_CASE(glyphs_in_later_index_sectors_are_found),
        TEST_CASE(codepoints_outside_the_pack_are_missing),
        TEST_CASE(a_second_lookup_reads_nothing),
        TEST_CASE(the_header_is_read_once),
        TEST_CASE(a_codepoint_outside_the_face_reads_nothing),
        TEST_CASE(glyphs_on_both_sides_of_a_directory_sector_are_found),
        TEST_CASE(a_face_holds_more_than_sixteen_index_sectors),
        TEST_CASE(forgetting_reads_the_pack_again),
        TEST_CASE(a_pack_without_its_magic_is_ignored),
        TEST_CASE(a_pack_of_another_version_is_ignored),
        TEST_CASE(a_damaged_header_is_refused),
        TEST_CASE(a_header_whose_glyph_count_disagrees_with_its_directory_is_refused),
        TEST_CASE(a_header_that_puts_its_data_past_its_end_is_refused),
        TEST_CASE(a_damaged_directory_sector_finds_nothing_it_points_to),
        TEST_CASE(a_damaged_index_sector_loses_only_its_own_glyphs),
        TEST_CASE(a_sector_from_another_face_is_refused),
        TEST_CASE(a_bitmap_outside_the_data_is_refused),
        TEST_CASE(a_bitmap_running_past_the_end_is_refused),
        TEST_CASE(a_truncated_face_finds_only_what_it_still_holds),
        TEST_CASE(a_failed_read_finds_nothing),
        TEST_CASE(too_little_memory_keeps_packs_off),
        TEST_CASE(the_memory_holds_whole_sets_of_slots),
        TEST_CASE(the_compact_glyph_arena_keeps_the_same_slot_count),
        TEST_CASE(a_font_falls_back_to_its_pack),
        TEST_CASE(a_character_in_neither_falls_back_to_the_question_mark),
        TEST_CASE(without_packs_a_font_uses_the_question_mark),
        TEST_CASE(a_font_reads_the_face_it_names),
        TEST_CASE(a_font_without_a_pack_face_ignores_packs),
        TEST_CASE(a_missing_glyph_is_read_from_the_cjk_fallback_face),
        TEST_CASE(japanese_language_codes_try_the_japanese_face_before_chinese),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
