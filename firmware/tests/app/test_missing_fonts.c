#include <string.h>

#include "check.h"
#include "core/bytes.h"
#include "core/drive_path.h"
#include "default_theme.h"
#include "extra_font.h"
#include "fake_hal.h"
#include "gfx/font_pack.h"
#include "library_store.h"
#include "missing_fonts.h"
#include "settings_screens.h"
#include "test_glyph_face.h"

enum {
    LIBRARY_BYTES = 1024,
    STRINGS_AT = 128,
    STRINGS_ENTRY = 8 + 9 * 12,
    WORK_BYTES = 40960,
    FONT_FILE_BYTES = 64 * 1024,
    MEMORY_BYTES = 0x6800 - 1024
};

static const char EXTRA_PATH[] =
    DRIVE_PATH_INTERNAL_ROOT "/" FIRMWARE_DATA_DIRECTORY "/fonts/extra.font";
static const char *const COVERED[] = {"ko"};
static uint8_t library[LIBRARY_BYTES];
static uint8_t work[WORK_BYTES];
static uint8_t extra[FONT_FILE_BYTES];
static uint32_t memory[MEMORY_BYTES / 4];
static Theme theme;
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

static void add_library(const char *const *names, uint32_t count)
{
    memset(library, 0, sizeof library);
    memcpy(library, "DSDB", 4);
    bytes_put_le16(library + 4, 2);
    bytes_put_le16(library + 6, 10);
    uint32_t at = STRINGS_AT + 1;
    for (uint32_t index = 0; index < count; index++) {
        size_t length = strlen(names[index]) + 1;
        memcpy(library + at, names[index], length);
        at += (uint32_t)length;
    }
    bytes_put_le32(library + STRINGS_ENTRY, STRINGS_AT);
    bytes_put_le32(library + STRINGS_ENTRY + 4, at - STRINGS_AT);
    fake_hal_add_file(LIBRARY_STORE_PATH, library, at);
}

static void start(void)
{
    fake_hal_reset();
    font_pack_detach_extra();
    font_pack_attach((FontPackReader){read_nothing, NULL}, memory, sizeof memory);
    handle = (HalFile){.handle = -1};
    theme = BOARD_THEME;
}

static void attach_hangul(void)
{
    TestExtraFont font = {.languages = COVERED, .language_count = 1};
    for (uint32_t face = 0; face < FONT_PACK_EXTRA_FACES; face++) {
        font.faces[face] = (TestGlyphFace){200, 0xAC00, 1, 0, 0};
    }
    fake_hal_add_file(EXTRA_PATH, extra, test_extra_font_build(extra, FONT_FILE_BYTES, &font));
    extra_font_open(&handle);
}

static void a_scan_records_each_undrawable_character_once_in_code_point_order(void)
{
    static const char *const names[] = {"Busker \xEB\xB2\x84\xEC\x8A\xA4\xEC\xBB\xA4",
                                        "\xED\x95\x9C\xEA\xB5\xAD", "Abba",
                                        "Music/\xED\x95\x9C\xEA\xB5\xAD/song.flac"};
    start();
    add_library(names, 4);
    missing_fonts_record(work, sizeof work, theme.fonts.body);
    uint32_t size = 0;
    const uint8_t *text = fake_hal_file_bytes(MISSING_FONTS_PATH, &size);
    CHECK(text != NULL);
    CHECK_EQUAL(MISSING_FONTS_FILE_BYTES, size);
    static const char expected[] = "\xEA\xB5\xAD\xEB\xB2\x84\xEC\x8A\xA4\xEC\xBB\xA4\xED\x95\x9C";
    CHECK(text != NULL && memcmp(expected, text, sizeof expected - 1) == 0);
    for (uint32_t index = sizeof expected - 1; text != NULL && index < size; index++) {
        CHECK_EQUAL(0, text[index]);
    }
    const uint8_t *count = fake_hal_file_bytes(MISSING_FONTS_COUNT_PATH, &size);
    CHECK(count != NULL && bytes_le32(count) == 2);
    CHECK_EQUAL(2, missing_fonts_names(theme.fonts.body));
}

static void a_library_the_fonts_draw_writes_no_file(void)
{
    static const char *const names[] = {"Abba", "Caf\xC3\xA9", "Music/Abba/01.flac"};
    start();
    add_library(names, 3);
    missing_fonts_record(work, sizeof work, theme.fonts.body);
    uint32_t size = 0;
    CHECK(fake_hal_file_bytes(MISSING_FONTS_PATH, &size) == NULL);
    CHECK_EQUAL(0, missing_fonts_names(theme.fonts.body));
}

static void the_notice_goes_away_once_extra_fonts_draw_the_characters(void)
{
    static const char *const names[] = {"\xEA\xB0\x80\xEA\xB0\x81"};
    start();
    add_library(names, 1);
    missing_fonts_record(work, sizeof work, theme.fonts.body);
    CHECK_EQUAL(1, missing_fonts_names(theme.fonts.body));
    attach_hangul();
    CHECK(font_pack_extra_attached());
    CHECK_EQUAL(0, missing_fonts_names(theme.fonts.body));
    extra_font_close(&handle);
}

static void too_little_memory_or_a_damaged_library_records_nothing(void)
{
    static const char *const names[] = {"\xEA\xB0\x80"};
    start();
    add_library(names, 1);
    missing_fonts_record(work, 1024, theme.fonts.body);
    uint32_t size = 0;
    CHECK(fake_hal_file_bytes(MISSING_FONTS_PATH, &size) == NULL);
    library[0] = 'X';
    missing_fonts_record(work, sizeof work, theme.fonts.body);
    CHECK(fake_hal_file_bytes(MISSING_FONTS_PATH, &size) == NULL);
}

static void choosing_about_in_settings_counts_the_names_for_its_notice(void)
{
    static const char *const names[] = {"\xEA\xB0\x80", "\xEA\xB0\x81", "Abba"};
    static SettingsScreens screens;
    static SettingsStore store;
    static Player player;
    static Library songs;
    static PowerPolicy power;
    static Toast toast;
    static BluetoothLink link;
    static BluetoothScreen bluetooth;
    static Screen about;
    start();
    add_library(names, 3);
    missing_fonts_record(work, sizeof work, theme.fonts.body);
    store.settings = settings_defaults();
    player_init(&player, &songs);
    bluetooth_screen_init(&bluetooth, &link, &theme);
    SettingsContext context = {.store = &store,
                               .player = &player,
                               .power = &power,
                               .toast = &toast,
                               .bluetooth = &bluetooth,
                               .theme = &theme};
    settings_screens_init(&screens, context, &theme);
    screens.about = &about;
    const MenuItem *row = &screens.groups[SETTINGS_GROUP_ABOUT];
    CHECK(row->select(row->context).target == &about);
    CHECK_EQUAL(2, screens.missing_names);
    CHECK(strcmp("2 names need fonts. Connect to a computer to add them.",
                 screens.missing_notice) == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_scan_records_each_undrawable_character_once_in_code_point_order),
        TEST_CASE(a_library_the_fonts_draw_writes_no_file),
        TEST_CASE(the_notice_goes_away_once_extra_fonts_draw_the_characters),
        TEST_CASE(too_little_memory_or_a_damaged_library_records_nothing),
        TEST_CASE(choosing_about_in_settings_counts_the_names_for_its_notice),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
