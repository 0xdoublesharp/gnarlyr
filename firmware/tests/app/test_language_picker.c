#include <string.h>

#include "check.h"
#include "core/drive_path.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "extra_font.h"
#include "fake_hal.h"
#include "gfx/font_pack.h"
#include "language.h"
#include "language_picker.h"
#include "test_glyph_face.h"
#include "test_pack.h"

enum {
    FILE_CAPACITY = 64 * 1024,
    MEMORY_BYTES = 0x6800 - 1024,
    ENGLISH_ROW = 0,
    GERMAN_ROW = 1,
    KOREAN_ROW = 2,
    UNKNOWN_ROW = 3
};

static const char PATH[] = DRIVE_PATH_INTERNAL_ROOT "/" FIRMWARE_DATA_DIRECTORY "/fonts/extra.font";
static const char *const COVERED[] = {"ko"};
static uint8_t english[TEST_PACK_CAPACITY];
static uint8_t german[TEST_PACK_CAPACITY];
static uint8_t korean[TEST_PACK_CAPACITY];
static uint8_t unknown[TEST_PACK_CAPACITY];
static uint8_t extra[FILE_CAPACITY];
static uint32_t memory[MEMORY_BYTES / 4];
static LanguageList languages;
static LanguagePicker picker;
static Theme theme;
static HalFile handle;
static int chosen;

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

static void choose(void *context, int index)
{
    CHECK(context == &chosen);
    chosen = index;
}

static uint32_t flagged(uint8_t *pack, TestPackLanguage language)
{
    uint32_t size = test_pack_build(pack, language);
    pack[TEST_PACK_FLAGS_FIELD] = I18N_NEEDS_EXTRA_FONT;
    test_pack_seal(pack, size);
    return size;
}

static void add_extra_font(void)
{
    TestExtraFont font = {.languages = COVERED, .language_count = 1};
    for (uint32_t face = 0; face < FONT_PACK_EXTRA_FACES; face++) {
        font.faces[face] = (TestGlyphFace){8, 0xAC00, 1, 0, 0};
    }
    fake_hal_add_file(PATH, extra, test_extra_font_build(extra, FILE_CAPACITY, &font));
    extra_font_open(&handle);
}

static void open_picker(bool with_extra_font)
{
    fake_hal_reset();
    i18n_detach();
    font_pack_detach_extra();
    font_pack_attach((FontPackReader){read_nothing, NULL}, memory, sizeof memory);
    handle = (HalFile){.handle = -1};
    uint32_t size = test_pack_build(english, (TestPackLanguage){"en", "English", NULL});
    fake_hal_add_language_pack(0, english, size);
    fake_hal_add_language_file(
        "de.pack", german, test_pack_build(german, (TestPackLanguage){"de", "Deutsch", NULL}));
    fake_hal_add_language_file(
        "ko.pack", korean,
        flagged(korean, (TestPackLanguage){"ko", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4", NULL}));
    fake_hal_add_language_file("xx.pack", unknown,
                               flagged(unknown, (TestPackLanguage){"xx", "Xyz", NULL}));
    if (with_extra_font) {
        add_extra_font();
    }
    theme = BOARD_THEME;
    CHECK(language_load("en"));
    language_list(&languages);
    chosen = -1;
    LanguagePickerConfig config = {&languages, choose, &chosen, &theme};
    Screen *screen = language_picker_init(&picker, config);
    screen->enter(screen->context, 0);
}

static const char *label(int row)
{
    return picker.model.label(&picker, row);
}

static const char *detail(int row)
{
    const char *text = picker.model.detail(&picker, row);
    return text != NULL ? text : "";
}

static ScreenAction press(Key key)
{
    KeyEvent event = {.key = key, .gesture = KEY_GESTURE_PRESS};
    return picker.screen.handle_key(&picker, event, 100);
}

static void a_language_shows_its_own_name_and_its_name_in_the_menu_language(void)
{
    open_picker(false);
    CHECK_EQUAL(4, languages.count);
    CHECK(strcmp("English", label(ENGLISH_ROW)) == 0);
    CHECK(strcmp("\xE2\x9C\x93", detail(ENGLISH_ROW)) == 0);
    CHECK(strcmp("Deutsch", label(GERMAN_ROW)) == 0);
    CHECK(strcmp("German", detail(GERMAN_ROW)) == 0);
    CHECK_EQUAL(LIST_ROW_PLAIN, picker.model.role(&picker, GERMAN_ROW));
    CHECK_EQUAL(ENGLISH_ROW, picker.list.selected);
}

static void a_language_that_needs_fonts_shows_a_drawable_name_and_a_note(void)
{
    open_picker(false);
    CHECK(strcmp("Korean (ko)", label(KOREAN_ROW)) == 0);
    CHECK(strcmp("needs fonts", detail(KOREAN_ROW)) == 0);
    CHECK_EQUAL(LIST_ROW_NEEDS, picker.model.role(&picker, KOREAN_ROW));
    CHECK(strcmp("xx", label(UNKNOWN_ROW)) == 0);
    CHECK_EQUAL(LIST_ROW_NEEDS, picker.model.role(&picker, UNKNOWN_ROW));
}

static void play_on_a_language_that_needs_fonts_explains_and_chooses_nothing(void)
{
    open_picker(false);
    int rows = picker.list.visible_rows;
    list_view_select(&picker.list, KOREAN_ROW, 0);
    CHECK_EQUAL(SCREEN_ACTION_NONE, press(KEY_PLAY).type);
    CHECK_EQUAL(-1, chosen);
    CHECK_EQUAL(KOREAN_ROW, picker.explained);
    CHECK(strcmp("Korean needs fonts. Connect to a computer to add them.", picker.notice) == 0);
    CHECK(picker.list.visible_rows < rows || languages.count < rows);
    press(KEY_VOLUME_UP);
    CHECK_EQUAL(-1, picker.explained);
    list_view_select(&picker.list, GERMAN_ROW, 0);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(KEY_PLAY).type);
    CHECK_EQUAL(GERMAN_ROW, chosen);
}

static void a_language_the_extra_font_covers_is_an_ordinary_row(void)
{
    open_picker(true);
    CHECK(font_pack_extra_attached());
    CHECK(strcmp("\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4", label(KOREAN_ROW)) == 0);
    CHECK(strcmp("Korean", detail(KOREAN_ROW)) == 0);
    CHECK_EQUAL(LIST_ROW_PLAIN, picker.model.role(&picker, KOREAN_ROW));
    CHECK_EQUAL(LIST_ROW_NEEDS, picker.model.role(&picker, UNKNOWN_ROW));
    list_view_select(&picker.list, KOREAN_ROW, 0);
    press(KEY_PLAY);
    CHECK_EQUAL(KOREAN_ROW, chosen);
    extra_font_close(&handle);
}

static void the_exonym_matches_the_primary_language_code(void)
{
    i18n_detach();
    CHECK(strcmp("Chinese", language_exonym("zh-TW")) == 0);
    CHECK(strcmp("Korean", language_exonym("KO")) == 0);
    CHECK(language_exonym("x") == NULL);
    CHECK(language_exonym("kor") == NULL);
    CHECK(language_exonym("") == NULL);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_language_shows_its_own_name_and_its_name_in_the_menu_language),
        TEST_CASE(a_language_that_needs_fonts_shows_a_drawable_name_and_a_note),
        TEST_CASE(play_on_a_language_that_needs_fonts_explains_and_chooses_nothing),
        TEST_CASE(a_language_the_extra_font_covers_is_an_ordinary_row),
        TEST_CASE(the_exonym_matches_the_primary_language_code),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
