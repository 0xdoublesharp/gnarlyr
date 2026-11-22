#include <string.h>

#include "app/app.h"
#include "app/debug_loop.h"
#include "app_internal.h"
#include "app_screens.h"
#include "card_files.h"
#include "check.h"
#include "cover_cache.h"
#include "default_theme.h"
#include "diagnostics_rows.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "library_store.h"
#include "music_check.h"
#include "music_record.h"
#include "music_walk.h"
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
    SETTINGS_FORMAT_ROWS_UP = 6,
    SCRIPT_STEP_MS = 8,
    SCRIPT_MS = 100
};

static const char HOME[] = "Home";
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
    music_record_write(&(MusicRecord){.card = {.complete = true}});
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

static void start_app_with_style(NowPlayingStyle style)
{
    app_reset_for_test();
    fake_hal_reset();
    add_player_files();
    SettingsStore store;
    settings_store_load(&store);
    settings_set_now_playing_style(&store.settings, style);
    CHECK(settings_store_save(&store));
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
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

static void delete_and_clear_questions_belong_to_the_modules_that_draw_them(void)
{
    static AppScreens isolated;
    CHECK_EQUAL(
        SCREEN_MODULE_VIEWS,
        screen_modules_of(&isolated, &isolated.modules.views.delete_question.confirm.screen));
    CHECK_EQUAL(SCREEN_MODULE_LISTENING,
                screen_modules_of(&isolated, &isolated.modules.listening.listening.screen));
    CHECK_EQUAL(SCREEN_MODULE_LISTENING,
                screen_modules_of(&isolated, &isolated.modules.listening.moments.screen));
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

static void hold_play(void)
{
    fake_hal_queue_transition(KEY_PLAY, true, now_ms);
    for (int held = 0; held < 50; held++) {
        now_ms += 20;
        step();
    }
    fake_hal_queue_transition(KEY_PLAY, false, now_ms);
    now_ms += 10;
    step();
}

static void add_track_files(void)
{
    static FixtureLibrary fixture;
    Library *library = fixture_library_open(&fixture);
    for (uint32_t index = 0; index < FIXTURE_TRACKS; index++) {
        LibraryTrack track;
        char path[256] = "/";
        CHECK(library_read_track(library, index, &track));
        CHECK(library_read_string(library, track.path, path + 1, sizeof path - 1));
        fake_hal_add_file(path[1] == '/' ? path + 1 : path, NULL, 100);
    }
}

static void open_never_played_track_actions(void)
{
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_VOLUME_DOWN);
    tap(KEY_VOLUME_DOWN);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    move_down(3);
    tap(KEY_PLAY);
    CHECK(title_is("Never Played"));
    for (int frame = 0; frame < 50; frame++) {
        wait_ms(20);
    }
    wait_ms(SLIDE_DURATION_MS);
    hold_play();
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_VOLUME_DOWN);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    CHECK(title_is("Delete file?"));
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, (int)fake_hal_loaded_module());
}

static void a_smart_list_delete_question_swaps_to_views_and_back_on_cancel_or_delete(void)
{
    start_app();
    add_track_files();
    open_never_played_track_actions();
    tap(KEY_BACK);
    wait_ms(SLIDE_DURATION_MS);
    CHECK(!title_is("Delete file?"));
    tap(KEY_BACK);
    wait_ms(SLIDE_DURATION_MS);
    CHECK(title_is("Never Played"));
    CHECK_EQUAL(SCREEN_MODULE_LISTENING, (int)fake_hal_loaded_module());
    hold_play();
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_VOLUME_DOWN);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    CHECK(title_is("Delete file?"));
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, (int)fake_hal_loaded_module());
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    CHECK(toast_is("File deleted"));
    CHECK_EQUAL(SCREEN_MODULE_LISTENING, (int)fake_hal_loaded_module());
    CHECK(title_is("Never Played"));
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

static void a_meter_style_keeps_home_lists_and_now_playing_in_the_stage_module(void)
{
    start_app_with_style(NOW_PLAYING_VU);
    open_music();
    CHECK_EQUAL(SCREEN_MODULE_STAGE, (int)fake_hal_loaded_module());
    CHECK_EQUAL(1, fake_hal_module_loads());
    move_down(6);
    tap(KEY_PLAY);
    CHECK(title_is("All Songs"));
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    CHECK(title_is("Now Playing"));
    tap(KEY_BACK);
    CHECK(title_is("All Songs"));
    CHECK_EQUAL(SCREEN_MODULE_STAGE, (int)fake_hal_loaded_module());
    CHECK_EQUAL(1, fake_hal_module_loads());
}

static void opening_albums_and_genres_under_a_meter_style_needs_no_module_load(void)
{
    for (int row = 3; row <= 4; row++) {
        start_app_with_style(NOW_PLAYING_VU);
        open_music();
        move_down(row);
        tap(KEY_PLAY);
        wait_ms(SLIDE_DURATION_MS);
        tap(KEY_PLAY);
        wait_ms(SLIDE_DURATION_MS);
        CHECK_EQUAL(ALBUM_DEPTH, app_screen_depth());
        CHECK_EQUAL(SCREEN_MODULE_STAGE, (int)fake_hal_loaded_module());
        CHECK_EQUAL(1, fake_hal_module_loads());
    }
}

static int artist_loads(int row)
{
    start_app_with_style(NOW_PLAYING_VU);
    open_music();
    move_down(1);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    move_down(row);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    return fake_hal_module_loads();
}

static void artists_load_playing_only_for_an_album_strip(void)
{
    int single = 0;
    int strip = 0;
    for (int row = 0; row < 8; row++) {
        int loads = artist_loads(row);
        bool on_strip = SCREEN_MODULE_PLAYING == (int)fake_hal_loaded_module();
        CHECK_EQUAL(on_strip ? 2 : 1, loads);
        CHECK_EQUAL(ALBUM_DEPTH, app_screen_depth());
        strip += on_strip ? 1 : 0;
        single += on_strip ? 0 : 1;
    }
    CHECK(single > 0);
    CHECK(strip > 0);
}

static void cover_flow_under_a_meter_style_swaps_modules_and_comes_back_intact(void)
{
    start_app_with_style(NOW_PLAYING_SPECTRUM);
    open_cover_flow();
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK_EQUAL(2, fake_hal_module_loads());
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK_EQUAL(ALBUM_DEPTH, app_screen_depth());
    CHECK_EQUAL(SCREEN_MODULE_STAGE, (int)fake_hal_loaded_module());
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_BACK);
    tap(KEY_BACK);
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    tap(KEY_MENU);
    CHECK(title_is("Now Playing"));
    CHECK_EQUAL(SCREEN_MODULE_STAGE, (int)fake_hal_loaded_module());
    wait_ms(200);
    tap(KEY_BACK);
    CHECK(title_is("Cover Flow"));
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK_EQUAL(6, fake_hal_module_loads());
}

static void a_damaged_stage_module_leaves_meter_styles_on_the_playing_module(void)
{
    start_app_with_style(NOW_PLAYING_VU);
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_STAGE, true);
    open_cover_flow();
    CHECK(title_is("Cover Flow"));
    tap(KEY_PLAY);
    CHECK_EQUAL(ALBUM_DEPTH, app_screen_depth());
    tap(KEY_PLAY);
    wait_ms(200);
    CHECK(title_is("Now Playing"));
    CHECK(app_toast_text() == NULL || !toast_is(MISSING));
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK(fake_hal_module_refusals() <= 1);
}

static void play_from_cover_flow(void)
{
    open_cover_flow();
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    wait_ms(200);
    CHECK(title_is("Now Playing"));
}

static NowPlayingStyle saved_style(void)
{
    SettingsStore store;
    settings_store_load(&store);
    return settings_now_playing_style(&store.settings);
}

static void cycle_style_in_quick_settings(int presses, bool first)
{
    tap(KEY_MENU);
    CHECK(title_is("Quick Settings"));
    for (int count = 0; first && count < 3; count++) {
        tap(KEY_VOLUME_DOWN);
    }
    tap(KEY_PLAY);
    CHECK(title_is("Now Playing style"));
    for (int count = 0; count < presses; count++) {
        tap(KEY_VOLUME_UP);
    }
    tap(KEY_PLAY);
    CHECK(title_is("Quick Settings"));
    CHECK_EQUAL(SCREEN_MODULE_EQUALIZER, (int)fake_hal_loaded_module());
    tap(KEY_BACK);
    wait_ms(200);
    CHECK(title_is("Now Playing"));
}

static void quick_settings_swaps_the_now_playing_module_once_per_close(void)
{
    start_app_with_style(NOW_PLAYING_COVER);
    play_from_cover_flow();
    int loads = fake_hal_module_loads();
    cycle_style_in_quick_settings(2, true);
    CHECK_EQUAL(NOW_PLAYING_VU, saved_style());
    CHECK_EQUAL(SCREEN_MODULE_STAGE, (int)fake_hal_loaded_module());
    CHECK_EQUAL(loads + 2, fake_hal_module_loads());
    cycle_style_in_quick_settings(1, false);
    CHECK_EQUAL(NOW_PLAYING_SPECTRUM, saved_style());
    CHECK_EQUAL(SCREEN_MODULE_STAGE, (int)fake_hal_loaded_module());
    CHECK_EQUAL(loads + 4, fake_hal_module_loads());
    cycle_style_in_quick_settings(2, false);
    CHECK_EQUAL(NOW_PLAYING_CASSETTE, saved_style());
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK_EQUAL(loads + 6, fake_hal_module_loads());
    CHECK(app_toast_text() == NULL || !toast_is(MISSING));
}

static void a_meter_style_chosen_in_quick_settings_without_an_intact_stage_shows_cover(void)
{
    start_app_with_style(NOW_PLAYING_COVER);
    play_from_cover_flow();
    fake_hal_break_module(SCREEN_MODULE_STAGE, true);
    cycle_style_in_quick_settings(2, true);
    CHECK_EQUAL(NOW_PLAYING_VU, saved_style());
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK_EQUAL(1, fake_hal_module_refusals());
    CHECK(app_toast_text() == NULL || !toast_is(MISSING));
    cycle_style_in_quick_settings(1, false);
    CHECK_EQUAL(NOW_PLAYING_SPECTRUM, saved_style());
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK_EQUAL(1, fake_hal_module_refusals());
    CHECK(app_toast_text() == NULL || !toast_is(MISSING));
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
}

static void quick_settings_cycles_from_the_style_settings_chose(void)
{
    start_app_with_style(NOW_PLAYING_COVER);
    play_from_cover_flow();
    run_script("build any\nset Now Playing style=VU meters\n");
    CHECK_EQUAL(DEBUG_LINE_OK, debug_runner_script()->status[1]);
    CHECK(title_is("Now Playing"));
    CHECK_EQUAL(SCREEN_MODULE_STAGE, (int)fake_hal_loaded_module());
    cycle_style_in_quick_settings(1, true);
    CHECK_EQUAL(NOW_PLAYING_SPECTRUM, saved_style());
    run_script("build any\nset Now Playing style=Cassette\n");
    CHECK_EQUAL(DEBUG_LINE_OK, debug_runner_script()->status[1]);
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    cycle_style_in_quick_settings(1, false);
    CHECK_EQUAL(NOW_PLAYING_VU, saved_style());
    CHECK_EQUAL(SCREEN_MODULE_STAGE, (int)fake_hal_loaded_module());
}

static void start_app_with_menu_hold(uint8_t action)
{
    app_reset_for_test();
    fake_hal_reset();
    add_player_files();
    SettingsStore store;
    settings_store_load(&store);
    store.settings.long_press_action = action;
    CHECK(settings_store_save(&store));
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
}

static void open_sleep_list(void)
{
    tap(KEY_MENU);
    CHECK(title_is("Quick Settings"));
    move_down(5);
    tap(KEY_PLAY);
    CHECK(title_is("Sleep timer"));
    CHECK_EQUAL(SCREEN_MODULE_EQUALIZER, (int)fake_hal_loaded_module());
}

static void choose_from_the_sleep_list(void)
{
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK(title_is("Quick Settings"));
    CHECK(app_toast_text() != NULL);
}

static void a_menu_hold_shortcut_over_a_value_list_gives_the_slot_back_in_the_same_step(void)
{
    start_app_with_menu_hold(4);
    play_from_cover_flow();
    open_sleep_list();
    int loads = fake_hal_module_loads();
    fake_hal_queue_transition(KEY_MENU, true, now_ms);
    now_ms += 1000;
    step();
    CHECK(app_player_status().shuffle != SHUFFLE_OFF);
    CHECK(title_is("Sleep timer"));
    CHECK_EQUAL(SCREEN_MODULE_EQUALIZER, (int)fake_hal_loaded_module());
    CHECK_EQUAL(loads + 2, fake_hal_module_loads());
    fake_hal_queue_transition(KEY_MENU, false, now_ms);
    now_ms += 10;
    step();
    CHECK(title_is("Sleep timer"));
    choose_from_the_sleep_list();
}

static void a_dac_headphone_change_over_a_value_list_gives_the_slot_back_before_drawing(void)
{
    start_app_with_style(NOW_PLAYING_COVER);
    play_from_cover_flow();
    open_sleep_list();
    int loads = fake_hal_module_loads();
    AppDacSettings dac = app_dac_settings();
    dac.headphones = 0;
    app_remember_dac(dac);
    wait_ms(20);
    CHECK(title_is("Sleep timer"));
    CHECK_EQUAL(SCREEN_MODULE_EQUALIZER, (int)fake_hal_loaded_module());
    CHECK_EQUAL(loads + 2, fake_hal_module_loads());
    choose_from_the_sleep_list();
}

static void open_now_playing_from_all_songs(void)
{
    open_music();
    move_down(6);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
}

static void a_missing_now_playing_module_is_retried_at_most_every_two_seconds(void)
{
    start_app_with_style(NOW_PLAYING_VU);
    open_now_playing_from_all_songs();
    CHECK(title_is("Now Playing"));
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_STAGE, true);
    fake_hal_break_module(SCREEN_MODULE_PLAYING, true);
    int before = fake_hal_module_refusals();
    for (int pass = 0; pass < 20; pass++) {
        wait_ms(20);
    }
    int first = fake_hal_module_refusals() - before;
    CHECK(first >= 1 && first <= 2);
    CHECK(title_is("Now Playing"));
    PlayerState state = app_player_status().state;
    tap(KEY_PLAY);
    CHECK(app_player_status().state != state);
    tap(KEY_POWER);
    for (int pass = 0; pass < 100; pass++) {
        wait_ms(20);
    }
    CHECK(fake_hal_module_refusals() - before <= first + 1);
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
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK(fake_hal_module_loads() > loads);
}

static void a_background_cover_pass_waits_while_a_usb_connection_is_pending(void)
{
    start_app();
    const uint8_t pending[8] = {'D', 'S', 'S', 'G', 0, 1, 0, 1};
    CHECK(hal_file_write_at(MUSIC_CHECK_PATH, 512, pending, sizeof pending));
    app_start();
    fake_hal_set_usb_connect_pending(true);
    int loads = fake_hal_rescan_loads();
    wait_ms(3000);
    CHECK_EQUAL(loads, fake_hal_rescan_loads());
    fake_hal_set_usb_connect_pending(false);
    wait_ms(3000);
    CHECK(fake_hal_rescan_loads() > loads);
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
    tap(KEY_BACK);
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_PLAYING, true);
    wait_ms(SLIDE_DURATION_MS / 2);
    CHECK_EQUAL(MUSIC_DEPTH, app_screen_depth());
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

static void open_card_format(void)
{
    fake_hal_set_card(
        (HalCardInfo){.present = true, .sectors = 64u * 2 * 1024 * 1024, .generation = 1}, true);
    fake_hal_set_battery((HalBattery){.known = true, .percent = 80});
    open_settings();
    for (int count = 0; count < SETTINGS_FORMAT_ROWS_UP; count++) {
        tap(KEY_VOLUME_DOWN);
    }
    tap(KEY_PLAY);
}

static void card_format_opens_from_settings_in_its_own_module(void)
{
    start_app();
    open_card_format();
    CHECK(title_is("Format card"));
    CHECK_EQUAL(SCREEN_MODULE_FORMAT, (int)fake_hal_loaded_module());
    tap(KEY_PLAY);
    CHECK(title_is("Erase this card?"));
    tap(KEY_BACK);
    CHECK(title_is("Settings"));
    CHECK_EQUAL(SCREEN_MODULE_SETTINGS, (int)fake_hal_loaded_module());
    tap(KEY_PLAY);
    CHECK(title_is("Format card"));
    CHECK_EQUAL(SCREEN_MODULE_FORMAT, (int)fake_hal_loaded_module());
    CHECK_EQUAL(0, fake_hal_card_formats());
}

static void a_damaged_card_format_module_keeps_settings_and_erases_nothing(void)
{
    start_app();
    fake_hal_break_module(SCREEN_MODULE_FORMAT, true);
    open_card_format();
    CHECK(title_is("Settings"));
    CHECK(toast_is(MISSING));
    CHECK_EQUAL(SCREEN_MODULE_SETTINGS, (int)fake_hal_loaded_module());
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK(title_is("Settings"));
    CHECK_EQUAL(0, fake_hal_card_formats());
    fake_hal_break_module(SCREEN_MODULE_FORMAT, false);
    tap(KEY_PLAY);
    CHECK(title_is("Format card"));
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
    CHECK_EQUAL(SCREEN_MODULE_EQUALIZER, (int)fake_hal_loaded_module());
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

static void a_music_check_without_its_module_neither_rebuilds_nor_repeats(void)
{
    start_on_a_fresh_card();
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_EQUALIZER, true);
    app_request_rescan_if_music_changed();
    step();
    step();
    step();
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
    fake_hal_break_module(SCREEN_MODULE_EQUALIZER, false);
    int walks = fake_hal_dir_open_count();
    app_request_rescan_if_music_changed();
    step();
    CHECK(fake_hal_dir_open_count() > walks);
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
}

static void a_manual_rescan_without_the_check_module_says_so_instead_of_rebuilding(void)
{
    start_on_a_fresh_card();
    fake_hal_evict_module();
    fake_hal_break_module(SCREEN_MODULE_EQUALIZER, true);
    app_request_rescan();
    step();
    fake_hal_break_module(SCREEN_MODULE_EQUALIZER, false);
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
    CHECK(toast_is("Screen missing: reinstall update"));
}

static int walks_after_start(bool moved)
{
    fake_hal_reset();
    add_player_files();
    CHECK(hal_internal_file_reserve(MUSIC_GENERATION_PATH, 4096) == HAL_RESERVE_CREATED);
    if (moved) {
        music_check_bump_generation();
    }
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    int walks = fake_hal_dir_open_count();
    for (int index = 0; index < 20; index++) {
        now_ms += 50;
        step();
    }
    return fake_hal_dir_open_count() - walks;
}

static int walks_after_restart(void)
{
    app_start();
    int walks = fake_hal_dir_open_count();
    for (int index = 0; index < 20; index++) {
        now_ms += 50;
        step();
    }
    return fake_hal_dir_open_count() - walks;
}

static void an_unknown_internal_half_asks_for_one_music_check_at_start(void)
{
    fake_hal_reset();
    add_player_files();
    CHECK(hal_internal_file_reserve(MUSIC_GENERATION_PATH, 4096) == HAL_RESERVE_CREATED);
    fake_hal_set_empty_card(true);
    music_record_write(&(MusicRecord){.card = music_walk_fingerprint(MUSIC_WALK_QUIET),
                                      .internal_state = MUSIC_DRIVE_UNKNOWN});
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    CHECK(walks_after_restart() > 0);
    CHECK_EQUAL(0, walks_after_restart());
}

static void a_moved_internal_music_generation_asks_for_a_music_check_at_start(void)
{
    CHECK(walks_after_start(true) > 0);
}

static void a_matching_internal_music_generation_asks_for_nothing_at_start(void)
{
    CHECK_EQUAL(0, walks_after_start(false));
}

static void about_music_says_card_and_internal_for_a_combined_library(void)
{
    start_on_a_fresh_card();
    CHECK(strcmp(diagnostics_row_music(NULL), "Card") == 0);
    fake_hal_set_combined(true);
    CHECK(strcmp(diagnostics_row_music(NULL), "Card + internal") == 0);
    fake_hal_set_combined(false);
}

static void about_omits_the_old_usb_controls_and_raw_dac_rows(void)
{
    AppScreens screens = {0};
    AppOptions options = {0};
    diagnostics_menu_build(&screens, &options, &BOARD_THEME);
    const MenuConfig *menu = &screens.modules.views.diagnostics.config;
    CHECK_EQUAL(15, menu->item_count);
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
        TEST_CASE(about_music_says_card_and_internal_for_a_combined_library),
        TEST_CASE(delete_and_clear_questions_belong_to_the_modules_that_draw_them),
        TEST_CASE(a_smart_list_delete_question_swaps_to_views_and_back_on_cancel_or_delete),
        TEST_CASE(a_moved_internal_music_generation_asks_for_a_music_check_at_start),
        TEST_CASE(an_unknown_internal_half_asks_for_one_music_check_at_start),
        TEST_CASE(a_matching_internal_music_generation_asks_for_nothing_at_start),
        TEST_CASE(about_omits_the_old_usb_controls_and_raw_dac_rows),
        TEST_CASE(discovery_screen_memory_belongs_to_the_library_module),
        TEST_CASE(search_is_the_first_music_entry_and_loads_the_library_module),
        TEST_CASE(the_personal_tag_manager_opens_from_the_last_music_row),
        TEST_CASE(smart_lists_open_from_music_and_play_through_the_listening_module),
        TEST_CASE(home_art_loads_playing_and_cover_flow_reuses_it),
        TEST_CASE(a_meter_style_keeps_home_lists_and_now_playing_in_the_stage_module),
        TEST_CASE(opening_albums_and_genres_under_a_meter_style_needs_no_module_load),
        TEST_CASE(artists_load_playing_only_for_an_album_strip),
        TEST_CASE(cover_flow_under_a_meter_style_swaps_modules_and_comes_back_intact),
        TEST_CASE(a_damaged_stage_module_leaves_meter_styles_on_the_playing_module),
        TEST_CASE(quick_settings_swaps_the_now_playing_module_once_per_close),
        TEST_CASE(a_meter_style_chosen_in_quick_settings_without_an_intact_stage_shows_cover),
        TEST_CASE(quick_settings_cycles_from_the_style_settings_chose),
        TEST_CASE(a_menu_hold_shortcut_over_a_value_list_gives_the_slot_back_in_the_same_step),
        TEST_CASE(a_dac_headphone_change_over_a_value_list_gives_the_slot_back_before_drawing),
        TEST_CASE(a_missing_now_playing_module_is_retried_at_most_every_two_seconds),
        TEST_CASE(a_module_in_the_slot_is_not_loaded_again),
        TEST_CASE(a_resident_screen_opened_from_a_module_screen_needs_no_load),
        TEST_CASE(a_module_that_cannot_load_keeps_the_screen_before_it_and_says_so),
        TEST_CASE(after_an_installed_update_a_module_screen_asks_for_a_restart),
        TEST_CASE(a_module_still_in_the_slot_serves_its_screens_after_an_installed_update),
        TEST_CASE(a_module_lost_under_its_screen_is_loaded_again_before_the_screen_runs),
        TEST_CASE(a_background_cover_pass_gives_the_screen_its_module_back_before_drawing),
        TEST_CASE(a_background_cover_pass_waits_while_a_usb_connection_is_pending),
        TEST_CASE(a_screen_whose_module_is_gone_for_good_gives_way_to_home),
        TEST_CASE(going_back_to_a_module_screen_that_cannot_load_goes_home),
        TEST_CASE(a_slide_away_from_a_module_screen_survives_the_loss_of_the_module),
        TEST_CASE(a_slide_ends_at_once_when_the_leaving_screens_module_cannot_come_back),
        TEST_CASE(module_screens_are_built_again_after_the_app_restarts),
        TEST_CASE(a_start_screen_in_a_module_that_cannot_load_starts_on_the_screen_before_it),
        TEST_CASE(settings_returns_from_about_to_its_own_module),
        TEST_CASE(a_screen_of_another_module_takes_the_slot),
        TEST_CASE(card_format_opens_from_settings_in_its_own_module),
        TEST_CASE(a_damaged_card_format_module_keeps_settings_and_erases_nothing),
        TEST_CASE(a_module_that_cannot_load_does_not_cost_the_screen_under_it_its_own),
        TEST_CASE(a_setting_changed_by_a_script_gives_the_slot_back_to_the_screen_on_top),
        TEST_CASE(an_about_value_is_read_through_its_module),
        TEST_CASE(settings_that_cannot_load_leave_home_usable),
        TEST_CASE(the_music_check_walks_the_card_from_its_module),
        TEST_CASE(after_a_music_check_the_screen_on_top_gets_its_module_back),
        TEST_CASE(a_music_check_without_its_module_neither_rebuilds_nor_repeats),
        TEST_CASE(a_manual_rescan_without_the_check_module_says_so_instead_of_rebuilding),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
