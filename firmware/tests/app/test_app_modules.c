#include <string.h>

#include "app/app.h"
#include "app/debug_loop.h"
#include "app_internal.h"
#include "app_screens.h"
#include "card_files.h"
#include "check.h"
#include "cover_cache.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "library_store.h"
#include "music_check.h"
#include "screen_modules.h"
#include "settings/settings.h"
#include "settings_store.h"
#include "ui/slide.h"

enum {
    START_MS = 1000,
    KEY_HOLD_MS = 30,
    HOME_DEPTH = 1,
    MUSIC_DEPTH = 2,
    SETTINGS_DEPTH = 2,
    COVER_FLOW_DEPTH = 3,
    ALBUM_DEPTH = 4,
    HOME_SETTINGS_ROW = 5,
    SETTINGS_SHUFFLE_ROW = 1,
    SETTINGS_BLUETOOTH_ROW = 3,
    SCRIPT_STEP_MS = 8,
    SCRIPT_MS = 100
};

static const char HOME[] = FIRMWARE_SHORT_NAME;
static const char MISSING[] = "Screen missing: reinstall update";
static const char RESTART[] = "Restart to finish the update";

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
    fake_hal_add_file(MUSIC_CHECK_PATH, NULL, 4096);
    music_check_remember((MusicFingerprint){.complete = true});
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
    tap(KEY_VOLUME_UP);
}

static void open_cover_flow(void)
{
    open_music();
    tap(KEY_PLAY);
}

static void move_down(int times)
{
    for (int count = 0; count < times; count++) {
        tap(KEY_VOLUME_UP);
    }
}

static void open_settings(void)
{
    move_down(HOME_SETTINGS_ROW);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
}

static void run_script(const char *text)
{
    DebugScriptRun *run = debug_runner_script();
    DebugScriptHost host = {hal_system_version(), hal_system_restart_pending()};
    size_t length = strlen(text);
    memcpy(run->text, text, length);
    debug_script_load(run, (uint32_t)length, host);
    debug_runner_begin();
    for (uint32_t elapsed = 0; elapsed < SCRIPT_MS; elapsed += SCRIPT_STEP_MS) {
        wait_ms(SCRIPT_STEP_MS);
    }
}

static void discovery_screen_memory_belongs_to_the_library_module(void)
{
    static AppScreens isolated;
    CHECK_EQUAL(SCREEN_MODULE_LIBRARY,
                screen_modules_of(&isolated, &isolated.modules.library.discovery.screen));
}

static void search_is_the_first_music_entry_and_loads_the_library_module(void)
{
    start_app();
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_PLAY);
    CHECK(title_is("Search"));
    CHECK_EQUAL(SCREEN_MODULE_LIBRARY, (int)fake_hal_loaded_module());
    CHECK_EQUAL(3, app_screen_depth());
    tap(KEY_BACK);
    CHECK(title_is("Music"));
}

static void the_personal_tag_manager_opens_from_the_last_music_row(void)
{
    start_app();
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_VOLUME_DOWN);
    tap(KEY_PLAY);
    CHECK(title_is("My tags"));
    CHECK_EQUAL(SCREEN_MODULE_LIBRARY, (int)fake_hal_loaded_module());
    tap(KEY_BACK);
    CHECK(title_is("Music"));
}

static void smart_lists_open_from_music_and_play_through_the_listening_module(void)
{
    start_app();
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_VOLUME_DOWN);
    tap(KEY_VOLUME_DOWN);
    tap(KEY_PLAY);
    CHECK(title_is("Smart Lists"));
    CHECK_EQUAL(SCREEN_MODULE_LISTENING, (int)fake_hal_loaded_module());
    wait_ms(SLIDE_DURATION_MS);
    move_down(3);
    tap(KEY_PLAY);
    CHECK(title_is("Never Played"));
    for (int frame = 0; frame < 50; frame++) {
        wait_ms(20);
    }
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_PLAY);
    CHECK(title_is("Now Playing"));
    tap(KEY_BACK);
    CHECK(title_is("Never Played"));
    CHECK_EQUAL(SCREEN_MODULE_LISTENING, (int)fake_hal_loaded_module());
    tap(KEY_BACK);
    tap(KEY_BACK);
    CHECK(title_is("Music"));
}

static void home_art_loads_playing_and_cover_flow_reuses_it(void)
{
    start_app();
    open_music();
    CHECK_EQUAL(1, fake_hal_module_loads());
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    tap(KEY_PLAY);
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(COVER_FLOW_DEPTH, app_screen_depth());
    CHECK_EQUAL(1, fake_hal_module_loads());
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
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
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_PLAYING, true);
    tap(KEY_PLAY);
    CHECK(title_is("Music"));
    CHECK_EQUAL(MUSIC_DEPTH, app_screen_depth());
    CHECK(toast_is(MISSING));
    CHECK_EQUAL(1, fake_hal_module_loads());
    fake_hal_break_module(SCREEN_MODULE_PLAYING, false);
    tap(KEY_PLAY);
    CHECK(title_is("Cover Flow"));
}

static void after_an_installed_update_a_module_screen_asks_for_a_restart(void)
{
    start_app();
    open_music();
    fake_hal_evict_module();
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
    const char *toast = app_toast_text();
    fake_hal_evict_module();
    wait_ms(20);
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(2, fake_hal_module_loads());
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK(app_toast_text() == toast);
}

static void a_background_cover_pass_gives_the_screen_its_module_back_before_drawing(void)
{
    start_app();
    const uint8_t pending[8] = {'D', 'S', 'S', 'G', 0, 1, 0, 1};
    CHECK(hal_file_write_at(MUSIC_CHECK_PATH, 512, pending, sizeof pending));
    app_start();
    open_cover_flow();
    wait_ms(SLIDE_DURATION_MS);
    int loads = fake_hal_module_loads();
    wait_ms(3000);
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, (int)fake_hal_loaded_module());
    CHECK(fake_hal_module_loads() > loads);
}

static void a_screen_whose_module_is_gone_for_good_gives_way_to_home(void)
{
    start_app();
    open_cover_flow();
    wait_ms(SLIDE_DURATION_MS);
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_PLAYING, true);
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
    fake_hal_break_module(SCREEN_MODULE_PLAYING, true);
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
    const char *toast = app_toast_text();
    tap(KEY_PLAY);
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_PLAYING, true);
    wait_ms(SLIDE_DURATION_MS / 2);
    CHECK_EQUAL(ALBUM_DEPTH, app_screen_depth());
    CHECK_EQUAL(1, fake_hal_module_loads());
    CHECK(app_toast_text() == toast);
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
    fake_hal_break_module(SCREEN_MODULE_PLAYING, true);
    Settings settings = settings_defaults();
    settings.start_screen = START_COVER_FLOW;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    hal_internal_file_write(SETTINGS_STORE_PATH, bytes, sizeof bytes);
    app_start();
    CHECK(title_is("Music"));
    CHECK(toast_is(MISSING));
    fake_hal_break_module(SCREEN_MODULE_PLAYING, false);
    app_start();
    CHECK(title_is("Cover Flow"));
}

static void settings_returns_from_about_to_its_own_module(void)
{
    start_app();
    open_settings();
    CHECK(title_is("Settings"));
    CHECK_EQUAL(SCREEN_MODULE_SETTINGS, (int)fake_hal_loaded_module());
    tap(KEY_PLAY);
    CHECK(title_is("About"));
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, (int)fake_hal_loaded_module());
    tap(KEY_BACK);
    move_down(SETTINGS_SHUFFLE_ROW);
    tap(KEY_PLAY);
    CHECK(title_is("Shuffle"));
    tap(KEY_BACK);
    move_down(SETTINGS_BLUETOOTH_ROW - SETTINGS_SHUFFLE_ROW);
    tap(KEY_PLAY);
    CHECK(title_is("Bluetooth"));
    CHECK_EQUAL(4, fake_hal_module_loads());
}

static void a_screen_of_another_module_takes_the_slot(void)
{
    start_app();
    open_cover_flow();
    tap(KEY_BACK);
    tap(KEY_BACK);
    open_settings();
    CHECK(title_is("Settings"));
    CHECK_EQUAL(SCREEN_MODULE_SETTINGS, (int)fake_hal_loaded_module());
    CHECK_EQUAL(2, fake_hal_module_loads());
    tap(KEY_BACK);
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK_EQUAL(3, fake_hal_module_loads());
}

static void a_module_that_cannot_load_does_not_cost_the_screen_under_it_its_own(void)
{
    start_app();
    open_cover_flow();
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK(title_is("Now Playing"));
    tap(KEY_BACK);
    tap(KEY_BACK);
    CHECK(title_is("Cover Flow"));
    fake_hal_break_module(SCREEN_MODULE_SETTINGS, true);
    run_script("build any\nset Repeat=All\n");
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK_EQUAL(DEBUG_LINE_FAILED, debug_runner_script()->status[1]);
}

static void a_setting_changed_by_a_script_gives_the_slot_back_to_the_screen_on_top(void)
{
    start_app();
    open_cover_flow();
    wait_ms(SLIDE_DURATION_MS);
    run_script("build any\nset Repeat=All\n");
    CHECK_EQUAL(DEBUG_LINE_OK, debug_runner_script()->status[1]);
    CHECK_EQUAL(REPEAT_ALL, app_player_status().repeat);
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK_EQUAL(3, fake_hal_module_loads());
}

static void an_about_value_is_read_through_its_module(void)
{
    start_app();
    CHECK_EQUAL(1, fake_hal_module_loads());
    CHECK(app_diagnostics_value(1) != NULL);
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, (int)fake_hal_loaded_module());
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_VIEWS, true);
    CHECK(app_diagnostics_value(1) == NULL);
}

static void settings_that_cannot_load_leave_home_usable(void)
{
    start_app();
    fake_hal_break_module(SCREEN_MODULE_SETTINGS, true);
    open_settings();
    CHECK(title_is(HOME));
    CHECK(toast_is(MISSING));
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK(title_is("Music"));
}

static void start_on_a_fresh_card(void)
{
    fake_hal_reset();
    fake_hal_set_empty_card(true);
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    while (app_preparing()) {
        step();
    }
    step();
}

static void the_music_check_walks_the_card_from_its_module(void)
{
    start_on_a_fresh_card();
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
    fake_hal_evict_module();
    int loads = fake_hal_module_loads();
    int walks = fake_hal_dir_open_count();
    app_request_rescan_if_music_changed();
    step();
    CHECK(fake_hal_dir_open_count() > walks);
    CHECK_EQUAL(loads + 1, fake_hal_module_loads());
    CHECK_EQUAL(SCREEN_MODULE_LIBRARY, (int)fake_hal_loaded_module());
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
}

static void after_a_music_check_the_screen_on_top_gets_its_module_back(void)
{
    start_on_a_fresh_card();
    open_settings();
    tap(KEY_PLAY);
    CHECK(title_is("About"));
    app_request_rescan_if_music_changed();
    step();
    CHECK(title_is("About"));
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, (int)fake_hal_loaded_module());
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
}

static void a_music_check_that_cannot_walk_the_card_rescans_instead(void)
{
    start_on_a_fresh_card();
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_LIBRARY, true);
    int walks = fake_hal_dir_open_count();
    app_request_rescan_if_music_changed();
    step();
    CHECK_EQUAL(2, fake_hal_scratch_borrow_count());
    fake_hal_break_module(SCREEN_MODULE_LIBRARY, false);
    walks = fake_hal_dir_open_count();
    app_request_rescan_if_music_changed();
    step();
    CHECK(fake_hal_dir_open_count() > walks);
    CHECK_EQUAL(2, fake_hal_scratch_borrow_count());
}

static void about_omits_the_old_usb_controls_and_raw_dac_rows(void)
{
    AppScreens screens = {0};
    AppOptions options = {0};
    diagnostics_menu_build(&screens, &options, &BOARD_THEME);
    const MenuConfig *menu = &screens.modules.views.diagnostics.config;
    CHECK_EQUAL(14, menu->item_count);
    for (int index = 0; index < menu->item_count; index++) {
        const char *label = menu->items[index].label;
        CHECK(strcmp(label, "Connect USB") != 0);
        CHECK(strcmp(label, "USB sense") != 0);
        CHECK(strcmp(label, "DAC A") != 0);
        CHECK(strcmp(label, "DAC B") != 0);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(about_omits_the_old_usb_controls_and_raw_dac_rows),
        TEST_CASE(discovery_screen_memory_belongs_to_the_library_module),
        TEST_CASE(search_is_the_first_music_entry_and_loads_the_library_module),
        TEST_CASE(the_personal_tag_manager_opens_from_the_last_music_row),
        TEST_CASE(smart_lists_open_from_music_and_play_through_the_listening_module),
        TEST_CASE(home_art_loads_playing_and_cover_flow_reuses_it),
        TEST_CASE(a_module_in_the_slot_is_not_loaded_again),
        TEST_CASE(a_resident_screen_opened_from_a_module_screen_needs_no_load),
        TEST_CASE(a_module_that_cannot_load_keeps_the_screen_before_it_and_says_so),
        TEST_CASE(after_an_installed_update_a_module_screen_asks_for_a_restart),
        TEST_CASE(a_module_still_in_the_slot_serves_its_screens_after_an_installed_update),
        TEST_CASE(a_module_lost_under_its_screen_is_loaded_again_before_the_screen_runs),
        TEST_CASE(a_background_cover_pass_gives_the_screen_its_module_back_before_drawing),
        TEST_CASE(a_screen_whose_module_is_gone_for_good_gives_way_to_home),
        TEST_CASE(going_back_to_a_module_screen_that_cannot_load_goes_home),
        TEST_CASE(a_slide_away_from_a_module_screen_survives_the_loss_of_the_module),
        TEST_CASE(a_slide_ends_at_once_when_the_leaving_screens_module_cannot_come_back),
        TEST_CASE(module_screens_are_built_again_after_the_app_restarts),
        TEST_CASE(a_start_screen_in_a_module_that_cannot_load_starts_on_the_screen_before_it),
        TEST_CASE(settings_returns_from_about_to_its_own_module),
        TEST_CASE(a_screen_of_another_module_takes_the_slot),
        TEST_CASE(a_module_that_cannot_load_does_not_cost_the_screen_under_it_its_own),
        TEST_CASE(a_setting_changed_by_a_script_gives_the_slot_back_to_the_screen_on_top),
        TEST_CASE(an_about_value_is_read_through_its_module),
        TEST_CASE(settings_that_cannot_load_leave_home_usable),
        TEST_CASE(the_music_check_walks_the_card_from_its_module),
        TEST_CASE(after_a_music_check_the_screen_on_top_gets_its_module_back),
        TEST_CASE(a_music_check_that_cannot_walk_the_card_rescans_instead),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
