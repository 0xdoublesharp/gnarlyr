#include <string.h>

#include "app/app.h"
#include "app_internal.h"
#include "check.h"
#include "core/drive_path.h"
#include "core/hash.h"
#include "extra_font.h"
#include "fake_hal.h"
#include "gfx/font_pack.h"
#include "test_glyph_face.h"

enum {
    FILE_CAPACITY = 64 * 1024,
    MEMORY_BYTES = 0x6800 - 1024,
    HANGUL = 0xAC00,
    EXTRA_FACE = FONT_PACK_IMAGE_FACES
};

static const char PATH[] = DRIVE_PATH_INTERNAL_ROOT "/" FIRMWARE_DATA_DIRECTORY "/fonts/extra.font";
static const char *const LANGUAGES[] = {"ko", "zh-TW"};
static uint8_t file_bytes[FILE_CAPACITY];
static uint32_t memory[MEMORY_BYTES / 4];
static HalFile handle;

static bool read_nothing(void *context, uint32_t face, uint32_t offset, void *buffer,
                         uint32_t length)
{
    (void)context;
    (void)face;
    (void)offset;
    (void)buffer;
    (void)length;
    return false;
}

static uint32_t build_file(uint32_t first)
{
    TestExtraFont font = {.languages = LANGUAGES, .language_count = 2};
    for (uint32_t face = 0; face < FONT_PACK_EXTRA_FACES; face++) {
        font.faces[face] = (TestGlyphFace){200, first, 1};
    }
    return test_extra_font_build(file_bytes, FILE_CAPACITY, &font);
}

static void start(uint32_t size)
{
    fake_hal_reset();
    font_pack_detach_extra();
    font_pack_attach((FontPackReader){read_nothing, NULL}, memory, sizeof memory);
    handle = (HalFile){.handle = -1};
    if (size > 0) {
        fake_hal_add_file(PATH, file_bytes, size);
    }
}

static uint8_t level_of(uint32_t codepoint)
{
    const Glyph *glyph = font_pack_find(EXTRA_FACE, codepoint);
    const uint8_t *bitmap = glyph != NULL ? font_pack_bitmap(glyph) : NULL;
    return bitmap != NULL ? font_bitmap_level(bitmap, glyph, 0, 0) : 0xFF;
}

static void the_file_on_internal_storage_is_attached(void)
{
    start(build_file(HANGUL));
    extra_font_open(&handle);
    CHECK(font_pack_extra_attached());
    CHECK_EQUAL(1, fake_hal_open_files());
    CHECK_EQUAL(5, level_of(HANGUL + 5));
    const char *language = font_pack_extra_language(1);
    CHECK(language != NULL && strcmp(language, "zh-TW") == 0);
}

static void a_missing_file_leaves_extra_fonts_off(void)
{
    start(0);
    extra_font_open(&handle);
    CHECK(!font_pack_extra_attached());
    CHECK_EQUAL(0, fake_hal_open_files());
    CHECK(font_pack_find(EXTRA_FACE, HANGUL) == NULL);
}

static void a_damaged_or_truncated_file_is_closed_again(void)
{
    uint32_t size = build_file(HANGUL);
    file_bytes[40] ^= 1;
    start(size);
    extra_font_open(&handle);
    CHECK(!font_pack_extra_attached());
    CHECK_EQUAL(0, fake_hal_open_files());
    size = build_file(HANGUL);
    start(size - 1);
    extra_font_open(&handle);
    CHECK(!font_pack_extra_attached());
    CHECK_EQUAL(0, fake_hal_open_files());
}

static void opening_twice_keeps_one_handle(void)
{
    start(build_file(HANGUL));
    extra_font_open(&handle);
    extra_font_open(&handle);
    CHECK_EQUAL(1, fake_hal_open_files());
}

static void closing_detaches_and_releases_the_handle_once(void)
{
    start(build_file(HANGUL));
    extra_font_open(&handle);
    extra_font_close(&handle);
    CHECK(!font_pack_extra_attached());
    CHECK_EQUAL(0, fake_hal_open_files());
    extra_font_close(&handle);
    CHECK_EQUAL(0, fake_hal_open_files());
}

static void a_handle_lost_to_a_storage_reset_is_never_closed(void)
{
    start(build_file(HANGUL));
    extra_font_open(&handle);
    font_pack_detach_extra();
    extra_font_close(&handle);
    CHECK_EQUAL(1, fake_hal_open_files());
}

static void a_replaced_file_is_read_after_reopening(void)
{
    start(build_file(HANGUL));
    extra_font_open(&handle);
    CHECK_EQUAL(1, level_of(HANGUL + 1));
    extra_font_close(&handle);
    uint32_t size = build_file(HANGUL + 1);
    start(size);
    extra_font_open(&handle);
    CHECK_EQUAL(0, level_of(HANGUL + 1));
}

static void nothing_is_read_while_a_restart_is_pending(void)
{
    start(build_file(HANGUL));
    fake_hal_set_restart_pending(true);
    extra_font_open(&handle);
    CHECK(!font_pack_extra_attached());
    CHECK_EQUAL(0, fake_hal_open_files());
    fake_hal_set_restart_pending(false);
    extra_font_open(&handle);
    fake_hal_set_restart_pending(true);
    CHECK(font_pack_find(EXTRA_FACE, HANGUL + 3) == NULL);
}

static void a_restart_pending_after_usb_keeps_the_held_glyphs(void)
{
    start(build_file(HANGUL));
    extra_font_open(&handle);
    font_pack_hold(FONT_PACK_HOLD_PIN);
    CHECK(font_pack_find(EXTRA_FACE, HANGUL + 7) != NULL);
    font_pack_hold(FONT_PACK_HOLD_FREEZE);
    fake_hal_set_restart_pending(true);
    app_hold_usb_glyphs(false);
    CHECK(font_pack_find(EXTRA_FACE, HANGUL + 7) != NULL);
    fake_hal_set_restart_pending(false);
    app_hold_usb_glyphs(false);
    CHECK_EQUAL(0, font_pack_held_bytes());
}

static void the_app_opens_it_at_start_and_lets_go_when_leaving_storage(void)
{
    start(build_file(HANGUL));
    app_reset_for_test();
    app_start();
    CHECK(font_pack_extra_attached());
    app_leave_storage();
    CHECK(!font_pack_extra_attached());
    app_start_after_storage();
    CHECK(font_pack_extra_attached());
    CHECK_EQUAL(7, level_of(HANGUL + 7));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_file_on_internal_storage_is_attached),
        TEST_CASE(a_missing_file_leaves_extra_fonts_off),
        TEST_CASE(a_damaged_or_truncated_file_is_closed_again),
        TEST_CASE(opening_twice_keeps_one_handle),
        TEST_CASE(closing_detaches_and_releases_the_handle_once),
        TEST_CASE(a_handle_lost_to_a_storage_reset_is_never_closed),
        TEST_CASE(a_replaced_file_is_read_after_reopening),
        TEST_CASE(nothing_is_read_while_a_restart_is_pending),
        TEST_CASE(a_restart_pending_after_usb_keeps_the_held_glyphs),
        TEST_CASE(the_app_opens_it_at_start_and_lets_go_when_leaving_storage),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
