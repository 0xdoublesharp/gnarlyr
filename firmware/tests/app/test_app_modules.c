#include <string.h>

#include "app/app.h"
#include "app_internal.h"
#include "card_files.h"
#include "check.h"
#include "cover_cache.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "hal/storage.h"
#include "library_store.h"
#include "screen_modules.h"
#include "settings/settings.h"
#include "settings_store.h"
#include "ui/slide.h"

enum {
    START_MS = 1000,
    KEY_HOLD_MS = 30,
    HOME_DEPTH = 1,
    MUSIC_DEPTH = 2,
    COVER_FLOW_DEPTH = 3,
    ALBUM_DEPTH = 4
};

static const char HOME[] = FIRMWARE_SHORT_NAME;
static const char MISSING[] = "Couldn't open this screen: reinstall the update";
static const char RESTART[] = "Restart the player to finish the update";

static uint32_t now_ms;

static void add_player_files(void)
{
    uint32_t size = 0;
    uint32_t covers_size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    const uint8_t *covers = fixture_covers_bytes(&covers_size);
    fake_hal_add_file(LIBRARY_STORE_PATH, bytes, size);
    fake_hal_add_file(COVER_CACHE_PATH, covers, covers_size);
    for (size_t index = 0; index < card_files_count(); index++) {
        HalFile file;
        if (card_files_slow(index) && !hal_file_open(&file, card_files_path(index))) {
            fake_hal_add_file(card_files_path(index), NULL, 4096);
        }
    }
}

static void start_app(void)
{
    fake_hal_reset();
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

static void wait_ms(uint32_t duration_ms)
{
    now_ms += duration_ms;
    step();
}

static bool title_is(const char *expected)
{
    return strcmp(expected, app_screen_title()) == 0;
}

static bool toast_is(const char *expected)
{
    return app_toast_text() != NULL && strcmp(expected, app_toast_text()) == 0;
}

static void open_music(void)
{
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
}

static void open_cover_flow(void)
{
    open_music();
    tap(KEY_PLAY);
}

static void a_module_screen_loads_its_module_when_it_opens(void)
{
    start_app();
    open_music();
    CHECK_EQUAL(0, fake_hal_module_loads());
    CHECK_EQUAL(SCREEN_MODULE_NONE, (int)fake_hal_loaded_module());
    tap(KEY_PLAY);
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(COVER_FLOW_DEPTH, app_screen_depth());
    CHECK_EQUAL(1, fake_hal_module_loads());
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, (int)fake_hal_loaded_module());
}

static void a_module_in_the_slot_is_not_loaded_again(void)
{
    start_app();
    open_cover_flow();
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_VOLUME_UP);
    tap(KEY_BACK);
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_PLAY);
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(1, fake_hal_module_loads());
}

static void a_resident_screen_opened_from_a_module_screen_needs_no_load(void)
{
    start_app();
    open_cover_flow();
    tap(KEY_PLAY);
    CHECK_EQUAL(ALBUM_DEPTH, app_screen_depth());
    tap(KEY_BACK);
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(1, fake_hal_module_loads());
}

static void a_module_that_cannot_load_keeps_the_screen_before_it_and_says_so(void)
{
    start_app();
    open_music();
    fake_hal_break_module(SCREEN_MODULE_VIEWS, true);
    tap(KEY_PLAY);
    CHECK(title_is("Music"));
    CHECK_EQUAL(MUSIC_DEPTH, app_screen_depth());
    CHECK(toast_is(MISSING));
    CHECK_EQUAL(0, fake_hal_module_loads());
    fake_hal_break_module(SCREEN_MODULE_VIEWS, false);
    tap(KEY_PLAY);
    CHECK(title_is("Cover Flow"));
}

static void after_an_installed_update_a_module_screen_asks_for_a_restart(void)
{
    start_app();
    open_music();
    fake_hal_set_restart_pending(true);
    tap(KEY_PLAY);
    CHECK(title_is("Music"));
    CHECK(toast_is(RESTART));
}

static void a_module_still_in_the_slot_serves_its_screens_after_an_installed_update(void)
{
    start_app();
    open_cover_flow();
    tap(KEY_BACK);
    fake_hal_set_restart_pending(true);
    tap(KEY_PLAY);
    CHECK(title_is("Cover Flow"));
}

static void a_module_lost_under_its_screen_is_loaded_again_before_the_screen_runs(void)
{
    start_app();
    open_cover_flow();
    wait_ms(SLIDE_DURATION_MS);
    fake_hal_evict_module();
    wait_ms(20);
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(2, fake_hal_module_loads());
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, (int)fake_hal_loaded_module());
    CHECK(app_toast_text() == NULL);
}

static void a_screen_whose_module_is_gone_for_good_gives_way_to_home(void)
{
    start_app();
    open_cover_flow();
    wait_ms(SLIDE_DURATION_MS);
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_VIEWS, true);
    wait_ms(20);
    CHECK(title_is(HOME));
    CHECK_EQUAL(HOME_DEPTH, app_screen_depth());
    CHECK(toast_is(MISSING));
    wait_ms(20);
    tap(KEY_PLAY);
    CHECK(title_is("Music"));
}

static void going_back_to_a_module_screen_that_cannot_load_goes_home(void)
{
    start_app();
    open_cover_flow();
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    CHECK_EQUAL(ALBUM_DEPTH, app_screen_depth());
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_VIEWS, true);
    tap(KEY_BACK);
    CHECK(title_is(HOME));
    CHECK_EQUAL(HOME_DEPTH, app_screen_depth());
    CHECK(toast_is(MISSING));
}

static void a_slide_away_from_a_module_screen_survives_the_loss_of_the_module(void)
{
    start_app();
    open_cover_flow();
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_PLAY);
    fake_hal_evict_module();
    wait_ms(SLIDE_DURATION_MS / 2);
    CHECK_EQUAL(ALBUM_DEPTH, app_screen_depth());
    CHECK_EQUAL(2, fake_hal_module_loads());
}

static void a_slide_ends_at_once_when_the_leaving_screens_module_cannot_come_back(void)
{
    start_app();
    open_cover_flow();
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_PLAY);
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_VIEWS, true);
    wait_ms(SLIDE_DURATION_MS / 2);
    CHECK_EQUAL(ALBUM_DEPTH, app_screen_depth());
    CHECK_EQUAL(1, fake_hal_module_loads());
    CHECK(app_toast_text() == NULL);
}

static void module_screens_are_built_again_after_the_app_restarts(void)
{
    start_app();
    open_cover_flow();
    now_ms += 10;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    CHECK(title_is(HOME));
    open_cover_flow();
    CHECK(title_is("Cover Flow"));
    tap(KEY_PLAY);
    CHECK_EQUAL(ALBUM_DEPTH, app_screen_depth());
    CHECK_EQUAL(1, fake_hal_module_loads());
}

static void a_start_screen_in_a_module_that_cannot_load_starts_on_the_screen_before_it(void)
{
    start_app();
    open_cover_flow();
    CHECK(title_is("Cover Flow"));
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_VIEWS, true);
    Settings settings = settings_defaults();
    settings.start_screen = START_COVER_FLOW;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    hal_internal_file_write(SETTINGS_STORE_PATH, bytes, sizeof bytes);
    app_start();
    CHECK(title_is("Music"));
    CHECK(toast_is(MISSING));
    fake_hal_break_module(SCREEN_MODULE_VIEWS, false);
    app_start();
    CHECK(title_is("Cover Flow"));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_module_screen_loads_its_module_when_it_opens),
        TEST_CASE(a_module_in_the_slot_is_not_loaded_again),
        TEST_CASE(a_resident_screen_opened_from_a_module_screen_needs_no_load),
        TEST_CASE(a_module_that_cannot_load_keeps_the_screen_before_it_and_says_so),
        TEST_CASE(after_an_installed_update_a_module_screen_asks_for_a_restart),
        TEST_CASE(a_module_still_in_the_slot_serves_its_screens_after_an_installed_update),
        TEST_CASE(a_module_lost_under_its_screen_is_loaded_again_before_the_screen_runs),
        TEST_CASE(a_screen_whose_module_is_gone_for_good_gives_way_to_home),
        TEST_CASE(going_back_to_a_module_screen_that_cannot_load_goes_home),
        TEST_CASE(a_slide_away_from_a_module_screen_survives_the_loss_of_the_module),
        TEST_CASE(a_slide_ends_at_once_when_the_leaving_screens_module_cannot_come_back),
        TEST_CASE(module_screens_are_built_again_after_the_app_restarts),
        TEST_CASE(a_start_screen_in_a_module_that_cannot_load_starts_on_the_screen_before_it),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
