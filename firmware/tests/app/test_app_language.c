#include <string.h>

#include "app/app.h"
#include "app_internal.h"
#include "card_files.h"
#include "check.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "settings/settings.h"
#include "settings_store.h"
#include "test_pack.h"

enum {
    START_MS = 1000,
    KEY_HOLD_MS = 50,
    HOME_SETTINGS_INDEX = 1,
    SETTINGS_ORIENTATION_INDEX = 10,
    SETTINGS_LANGUAGE_INDEX = 11,
    SETTINGS_DEPTH = 2,
    MUSIC_ALL_SONGS_INDEX = 6
};

static uint8_t english[TEST_PACK_CAPACITY];
static uint8_t upper[TEST_PACK_CAPACITY];
static uint8_t bracketed[TEST_PACK_CAPACITY];
static uint32_t english_size;
static uint32_t upper_size;
static uint32_t bracketed_size;
static uint32_t now_ms;

static void build_packs(void)
{
    english_size = test_pack_build(english, (TestPackLanguage){"en", "English", NULL});
    upper_size = test_pack_build(upper, (TestPackLanguage){"up", "UPPER", test_pack_upper_case});
    bracketed_size =
        test_pack_build(bracketed, (TestPackLanguage){"qps", "[Pseudo]", test_pack_bracketed});
}

static void add_player_files(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/library.db", bytes, size);
    for (size_t index = 0; index < card_files_count(); index++) {
        HalFile file;
        if (card_files_slow(index) && !hal_file_open(&file, card_files_path(index))) {
            fake_hal_add_file(card_files_path(index), NULL, 4096);
        }
    }
}

static void start_with(int built_in, bool with_file)
{
    fake_hal_reset();
    i18n_detach();
    if (built_in >= 1) {
        fake_hal_add_language_pack(0, english, english_size);
    }
    if (built_in >= 2) {
        fake_hal_add_language_pack(0, upper, upper_size);
    }
    if (with_file) {
        fake_hal_add_language_file("qps.pack", bracketed, bracketed_size);
    }
    add_player_files();
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
}

static void step(void)
{
    fake_hal_set_milliseconds(now_ms);
    app_step();
}

static void tap(Key key)
{
    fake_hal_queue_transition(key, true, now_ms);
    fake_hal_queue_transition(key, false, now_ms + KEY_HOLD_MS);
    now_ms += KEY_HOLD_MS + 10;
    step();
}

static void move_down(int times)
{
    for (int count = 0; count < times; count++) {
        tap(KEY_VOLUME_UP);
    }
}

static void wait_ms(uint32_t duration_ms)
{
    now_ms += duration_ms;
    step();
}

static bool title_is(const char *expected)
{
    return strcmp(expected, app_screen_title()) == 0;
}

static void open_setting(int index)
{
    move_down(HOME_SETTINGS_INDEX);
    tap(KEY_PLAY);
    move_down(index);
    tap(KEY_PLAY);
}

static void choose_language(int option)
{
    open_setting(SETTINGS_LANGUAGE_INDEX);
    move_down(option);
    tap(KEY_PLAY);
    step();
}

static bool saved_language_is(const char *code)
{
    uint32_t size = 0;
    Settings settings;
    const uint8_t *bytes = fake_hal_internal_file_bytes(SETTINGS_STORE_PATH, &size);
    return bytes != NULL && settings_decode(bytes, size, &settings) &&
           strcmp(code, settings.language) == 0;
}

static void the_app_loads_the_images_pack_at_start(void)
{
    start_with(1, false);
    CHECK(i18n_attached());
    CHECK(strcmp("en", i18n_code()) == 0);
    tap(KEY_PLAY);
    CHECK(title_is("Music"));
}

static void with_one_language_settings_has_no_language_row(void)
{
    start_with(1, false);
    open_setting(SETTINGS_LANGUAGE_INDEX);
    CHECK(title_is("Skip songs"));
    start_with(0, false);
    open_setting(SETTINGS_LANGUAGE_INDEX);
    CHECK(title_is("Skip songs"));
}

static void with_two_languages_settings_has_a_language_row_after_orientation(void)
{
    start_with(2, false);
    open_setting(SETTINGS_ORIENTATION_INDEX);
    CHECK(title_is("Orientation"));
    tap(KEY_BACK);
    move_down(1);
    tap(KEY_PLAY);
    CHECK(title_is("Language"));
    tap(KEY_BACK);
    move_down(1);
    tap(KEY_PLAY);
    CHECK(title_is("Skip songs"));
}

static void choosing_a_language_changes_the_screens_at_once(void)
{
    start_with(2, false);
    choose_language(1);
    CHECK(strcmp("up", i18n_code()) == 0);
    CHECK(title_is("SETTINGS"));
    CHECK_EQUAL(SETTINGS_DEPTH, app_screen_depth());
    tap(KEY_PLAY);
    CHECK(title_is("LANGUAGE"));
    tap(KEY_BACK);
    tap(KEY_BACK);
    tap(KEY_PLAY);
    CHECK(title_is("MUSIC"));
}

static void the_chosen_language_is_saved_and_loaded_at_the_next_start(void)
{
    start_with(2, false);
    choose_language(1);
    CHECK(saved_language_is("up"));
    i18n_detach();
    now_ms += 10;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    CHECK(strcmp("up", i18n_code()) == 0);
    tap(KEY_PLAY);
    CHECK(title_is("MUSIC"));
}

static void choosing_the_loaded_language_again_keeps_it(void)
{
    start_with(2, false);
    choose_language(0);
    CHECK(strcmp("en", i18n_code()) == 0);
    CHECK(title_is("Settings"));
    CHECK(app_toast_text() == NULL);
}

static void a_language_file_in_the_data_folder_is_offered_and_loads(void)
{
    start_with(1, true);
    choose_language(1);
    CHECK(strcmp("qps", i18n_code()) == 0);
    CHECK(title_is("[Settings]"));
    CHECK(saved_language_is("qps"));
}

static void a_language_that_fails_its_checks_keeps_the_loaded_one_and_says_so(void)
{
    start_with(1, true);
    bracketed[bracketed_size - 4] ^= 1;
    choose_language(1);
    bracketed[bracketed_size - 4] ^= 1;
    CHECK(strcmp("en", i18n_code()) == 0);
    CHECK(title_is("Settings"));
    CHECK(app_toast_text() != NULL && strcmp("Couldn't load that language", app_toast_text()) == 0);
    CHECK(saved_language_is("en"));
}

static void a_saved_language_whose_file_is_gone_starts_in_the_default(void)
{
    start_with(1, true);
    choose_language(1);
    CHECK(saved_language_is("qps"));
    uint32_t size = 0;
    static uint8_t settings_bytes[SETTINGS_FILE_SIZE];
    memcpy(settings_bytes, fake_hal_internal_file_bytes(SETTINGS_STORE_PATH, &size),
           SETTINGS_FILE_SIZE);
    for (int with_file = 1; with_file >= 0; with_file--) {
        fake_hal_reset();
        i18n_detach();
        fake_hal_add_language_pack(0, english, english_size);
        if (with_file) {
            fake_hal_add_language_file("qps.pack", bracketed, bracketed_size);
        }
        fake_hal_add_file(SETTINGS_STORE_PATH, settings_bytes, SETTINGS_FILE_SIZE);
        add_player_files();
        app_start();
        CHECK(strcmp(with_file ? "qps" : "en", i18n_code()) == 0);
        tap(KEY_PLAY);
        CHECK(title_is(with_file ? "[Music]" : "Music"));
    }
}

static void changing_the_language_keeps_the_music_playing(void)
{
    start_with(2, false);
    tap(KEY_PLAY);
    move_down(MUSIC_ALL_SONGS_INDEX);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    int plays = fake_hal_audio_play_count();
    tap(KEY_BACK);
    tap(KEY_BACK);
    tap(KEY_BACK);
    wait_ms(400);
    choose_language(1);
    CHECK(title_is("SETTINGS"));
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
}

static void a_toast_from_before_the_change_is_dropped(void)
{
    start_with(2, false);
    open_setting(SETTINGS_LANGUAGE_INDEX - 7);
    move_down(1);
    tap(KEY_PLAY);
    CHECK(app_toast_text() != NULL);
    move_down(7);
    tap(KEY_PLAY);
    move_down(1);
    tap(KEY_PLAY);
    step();
    CHECK(strcmp("up", i18n_code()) == 0);
    CHECK(app_toast_text() == NULL);
}

int main(void)
{
    build_packs();
    static const TestCase cases[] = {
        TEST_CASE(the_app_loads_the_images_pack_at_start),
        TEST_CASE(with_one_language_settings_has_no_language_row),
        TEST_CASE(with_two_languages_settings_has_a_language_row_after_orientation),
        TEST_CASE(choosing_a_language_changes_the_screens_at_once),
        TEST_CASE(the_chosen_language_is_saved_and_loaded_at_the_next_start),
        TEST_CASE(choosing_the_loaded_language_again_keeps_it),
        TEST_CASE(a_language_file_in_the_data_folder_is_offered_and_loads),
        TEST_CASE(a_language_that_fails_its_checks_keeps_the_loaded_one_and_says_so),
        TEST_CASE(a_saved_language_whose_file_is_gone_starts_in_the_default),
        TEST_CASE(changing_the_language_keeps_the_music_playing),
        TEST_CASE(a_toast_from_before_the_change_is_dropped),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
