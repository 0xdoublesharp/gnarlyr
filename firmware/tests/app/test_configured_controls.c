#include <string.h>

#include "app/app.h"
#include "app_internal.h"
#include "card_files.h"
#include "check.h"
#include "cover_cache.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "library_store.h"
#include "settings/settings.h"
#include "settings_screens.h"
#include "settings_store.h"

enum {
    MUSIC_ALBUMS_INDEX = 4
};

static uint32_t now_ms;

static void step(void)
{
    fake_hal_set_milliseconds(now_ms);
    app_step();
}

static void wait_ms(uint32_t duration)
{
    now_ms += duration;
    step();
}

static void tap(Key key)
{
    fake_hal_queue_transition(key, true, now_ms);
    fake_hal_queue_transition(key, false, now_ms + 30);
    wait_ms(40);
}

static void hold(Key key, uint32_t duration)
{
    fake_hal_queue_transition(key, true, now_ms);
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += 40) {
        wait_ms(40);
    }
    fake_hal_queue_transition(key, false, now_ms);
    wait_ms(40);
}

static void add_player_files(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    fake_hal_add_file(LIBRARY_STORE_PATH, bytes, size);
    bytes = fixture_covers_bytes(&size);
    fake_hal_add_file(COVER_CACHE_PATH, bytes, size);
    for (size_t index = 0; index < card_files_count(); index++) {
        HalFile file;
        if (!card_files_slow(index)) {
            continue;
        }
        if (hal_file_open(&file, card_files_path(index))) {
            hal_file_close(&file);
        } else {
            fake_hal_add_file(card_files_path(index), NULL, 4096);
        }
    }
}

static void start_with(Settings chosen)
{
    fake_hal_reset();
    add_player_files();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    uint8_t buttons[SETTINGS_BUTTONS_FILE_SIZE];
    settings_encode(&chosen, bytes);
    settings_buttons_encode(&chosen, buttons);
    CHECK(hal_internal_file_write(SETTINGS_STORE_PATH, bytes, sizeof bytes));
    CHECK(hal_internal_file_write(SETTINGS_BUTTONS_PATH, buttons, sizeof buttons));
    now_ms = 1000;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    wait_ms(1200);
    tap(KEY_PLAY);
    for (int row = 0; row < MUSIC_ALBUMS_INDEX; row++) {
        tap(KEY_VOLUME_UP);
    }
    tap(KEY_PLAY);
    CHECK(strcmp(app_screen_title(), "Albums") == 0);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    wait_ms(400);
    CHECK(strcmp(app_screen_title(), "Now Playing") == 0);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
}

static void default_controls_still_pause_change_volume_and_open_quick_settings(void)
{
    start_with(settings_defaults());
    int volume = fake_hal_audio_volume();
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(volume + 1, fake_hal_audio_volume());
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    tap(KEY_MENU);
    CHECK(strcmp(app_screen_title(), "Quick Settings") == 0);
}

static void a_custom_play_tap_runs_once_without_also_pausing(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_PLAY_TAP] = BUTTON_ACTION_VOLUME_UP;
    start_with(settings);
    int volume = fake_hal_audio_volume();
    int plays = fake_hal_audio_play_count();
    tap(KEY_PLAY);
    wait_ms(400);
    CHECK_EQUAL(volume + 1, fake_hal_audio_volume());
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
}

static void a_custom_play_hold_runs_once_without_the_tap_action(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_PLAY_TAP] = BUTTON_ACTION_VOLUME_UP;
    settings.button_actions[PLAYBACK_PLAY_HOLD] = BUTTON_ACTION_NEXT;
    start_with(settings);
    int volume = fake_hal_audio_volume();
    int plays = fake_hal_audio_play_count();
    hold(KEY_PLAY, 2400);
    CHECK_EQUAL(plays + 1, fake_hal_audio_play_count());
    CHECK_EQUAL(volume, fake_hal_audio_volume());
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
}

static void holding_a_mapped_skip_does_not_skip_again_on_repeat_events(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_VOLUME_UP_HOLD] = BUTTON_ACTION_NEXT;
    start_with(settings);
    int volume = fake_hal_audio_volume();
    int plays = fake_hal_audio_play_count();
    hold(KEY_VOLUME_UP, 2600);
    CHECK_EQUAL(plays + 1, fake_hal_audio_play_count());
    CHECK_EQUAL(volume, fake_hal_audio_volume());
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
}

static void an_audio_mapping_works_with_the_screen_off_without_waking_it(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_PLAY_TAP] = BUTTON_ACTION_NEXT;
    start_with(settings);
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    int plays = fake_hal_audio_play_count();
    tap(KEY_PLAY);
    CHECK_EQUAL(plays + 1, fake_hal_audio_play_count());
    CHECK(!fake_hal_display_on());
}

static void a_navigation_mapping_wakes_the_screen_and_opens_its_destination(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_MENU_TAP] = BUTTON_ACTION_UP_NEXT;
    start_with(settings);
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    tap(KEY_MENU);
    CHECK(fake_hal_display_on());
    CHECK(strcmp(app_screen_title(), "Up Next") == 0);
}

static void a_home_mapping_wakes_the_screen_before_showing_home(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_MENU_TAP] = BUTTON_ACTION_HOME;
    start_with(settings);
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    tap(KEY_MENU);
    CHECK(fake_hal_display_on());
    CHECK(strcmp(app_screen_title(), FIRMWARE_SHORT_NAME) == 0);
}

static void reset_buttons_restores_the_live_controls_and_saved_defaults(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_PLAY_TAP] = BUTTON_ACTION_VOLUME_UP;
    start_with(settings);
    hold(KEY_BACK, 1000);
    tap(KEY_VOLUME_DOWN);
    tap(KEY_PLAY);
    CHECK(strcmp(app_screen_title(), "Settings") == 0);
    for (int group = 0; group < SETTINGS_GROUP_BUTTONS; group++) {
        tap(KEY_VOLUME_UP);
    }
    tap(KEY_PLAY);
    CHECK(strcmp(app_screen_title(), "Buttons") == 0);
    tap(KEY_VOLUME_DOWN);
    tap(KEY_PLAY);
    SettingsStore restored;
    settings_store_load(&restored);
    for (int button = 0; button < PLAYBACK_BUTTON_COUNT; button++) {
        CHECK_EQUAL(BUTTON_ACTION_DEFAULT, restored.settings.button_actions[button]);
    }
    tap(KEY_MENU);
    CHECK(strcmp(app_screen_title(), "Now Playing") == 0);
    int volume = fake_hal_audio_volume();
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    CHECK_EQUAL(volume, fake_hal_audio_volume());
}

static void chord_tap(Key volume)
{
    fake_hal_queue_transition(KEY_PLAY, true, now_ms);
    wait_ms(40);
    tap(volume);
    fake_hal_queue_transition(KEY_PLAY, false, now_ms);
    wait_ms(40);
}

static void a_custom_volume_tap_keeps_the_play_chord_skip(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_VOLUME_UP_TAP] = BUTTON_ACTION_PLAY_PAUSE;
    start_with(settings);
    uint16_t first = app_player_status().track;
    int volume = fake_hal_audio_volume();
    chord_tap(KEY_VOLUME_UP);
    CHECK(app_player_status().track != first);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK_EQUAL(volume, fake_hal_audio_volume());
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
}

static void a_custom_play_tap_does_not_run_after_a_chord(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_PLAY_TAP] = BUTTON_ACTION_UP_NEXT;
    start_with(settings);
    uint16_t first = app_player_status().track;
    chord_tap(KEY_VOLUME_UP);
    CHECK(app_player_status().track != first);
    CHECK(strcmp(app_screen_title(), "Now Playing") == 0);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
}

static void configured_a_b_round_trip(bool screen_off)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_MENU_HOLD] = BUTTON_ACTION_AB_REPEAT;
    start_with(settings);
    if (screen_off) {
        tap(KEY_POWER);
        CHECK(!fake_hal_display_on());
    }
    static const uint8_t states[] = {1, 2, 0};
    for (int index = 0; index < 3; index++) {
        hold(KEY_MENU, 900);
        CHECK_EQUAL(states[index], app_player_status().ab_repeat);
        CHECK_EQUAL(screen_off, !fake_hal_display_on());
        CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    }
    if (!screen_off) {
        CHECK(strcmp(app_screen_title(), "Now Playing") == 0);
    }
}

static void a_mapped_a_b_repeat_sets_a_then_b_then_clears(void)
{
    configured_a_b_round_trip(false);
    configured_a_b_round_trip(true);
}

static void a_mapped_play_hold_waits_for_release_so_a_chord_still_skips(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_PLAY_HOLD] = BUTTON_ACTION_HOME;
    start_with(settings);
    uint16_t first = app_player_status().track;
    fake_hal_queue_transition(KEY_PLAY, true, now_ms);
    for (int count = 0; count < 30; count++) {
        wait_ms(40);
    }
    CHECK(strcmp(app_screen_title(), "Now Playing") == 0);
    tap(KEY_VOLUME_UP);
    fake_hal_queue_transition(KEY_PLAY, false, now_ms);
    wait_ms(40);
    CHECK(app_player_status().track != first);
    CHECK(strcmp(app_screen_title(), "Now Playing") == 0);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    hold(KEY_PLAY, 900);
    wait_ms(1000);
    CHECK(strcmp(app_screen_title(), "Now Playing") != 0);
}

static void a_mapped_a_b_refused_b_says_why(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_MENU_HOLD] = BUTTON_ACTION_AB_REPEAT;
    start_with(settings);
    hold(KEY_MENU, 900);
    CHECK_EQUAL(1, app_player_status().ab_repeat);
    fake_hal_audio_refuse_repeat_b(true);
    hold(KEY_MENU, 900);
    CHECK_EQUAL(1, app_player_status().ab_repeat);
    CHECK(app_toast_text() != NULL && strcmp(app_toast_text(), "B must come after A") == 0);
    fake_hal_audio_refuse_repeat_b(false);
}

static void menu_chord(Key volume, uint32_t held_ms)
{
    fake_hal_queue_transition(KEY_MENU, true, now_ms);
    wait_ms(40);
    hold(volume, held_ms);
    fake_hal_queue_transition(KEY_MENU, false, now_ms);
    wait_ms(40);
}

static void a_lone_a_mark_stays_through_a_scrub_and_a_loop_declines_it(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_MENU_HOLD] = BUTTON_ACTION_AB_REPEAT;
    start_with(settings);
    hold(KEY_MENU, 900);
    CHECK_EQUAL(1, app_player_status().ab_repeat);
    int seeks = fake_hal_audio_seek_count();
    menu_chord(KEY_VOLUME_UP, 0);
    CHECK_EQUAL(seeks + 1, fake_hal_audio_seek_count());
    menu_chord(KEY_VOLUME_UP, 1600);
    CHECK_EQUAL(seeks + 2, fake_hal_audio_seek_count());
    CHECK_EQUAL(1, app_player_status().ab_repeat);
    hold(KEY_MENU, 900);
    CHECK_EQUAL(2, app_player_status().ab_repeat);
    menu_chord(KEY_VOLUME_UP, 0);
    CHECK(app_toast_text() != NULL && strcmp(app_toast_text(), "A-B repeat is on") == 0);
    menu_chord(KEY_VOLUME_DOWN, 1600);
    CHECK_EQUAL(seeks + 2, fake_hal_audio_seek_count());
    CHECK_EQUAL(2, app_player_status().ab_repeat);
    CHECK(strcmp(app_screen_title(), "Now Playing") == 0);
}

static void a_play_hold_begun_on_a_list_follows_the_screen_going_dark(void)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_PLAY_HOLD] = BUTTON_ACTION_PLAY_PAUSE;
    start_with(settings);
    tap(KEY_BACK);
    CHECK(strcmp(app_screen_title(), "Now Playing") != 0);
    uint16_t first = app_player_status().track;
    uint32_t timeout = settings_screen_timeout_ms(&settings);
    for (uint32_t waited = 0; waited + 300 < timeout; waited += 40) {
        wait_ms(40);
    }
    CHECK(fake_hal_display_on());
    fake_hal_queue_transition(KEY_PLAY, true, now_ms);
    for (int count = 0; count < 25; count++) {
        wait_ms(40);
    }
    CHECK(!fake_hal_display_on());
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    tap(KEY_VOLUME_UP);
    fake_hal_queue_transition(KEY_PLAY, false, now_ms);
    wait_ms(40);
    CHECK(app_player_status().track != first);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK(!fake_hal_display_on());
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(default_controls_still_pause_change_volume_and_open_quick_settings),
        TEST_CASE(a_custom_play_tap_runs_once_without_also_pausing),
        TEST_CASE(a_custom_play_hold_runs_once_without_the_tap_action),
        TEST_CASE(holding_a_mapped_skip_does_not_skip_again_on_repeat_events),
        TEST_CASE(an_audio_mapping_works_with_the_screen_off_without_waking_it),
        TEST_CASE(a_navigation_mapping_wakes_the_screen_and_opens_its_destination),
        TEST_CASE(a_home_mapping_wakes_the_screen_before_showing_home),
        TEST_CASE(reset_buttons_restores_the_live_controls_and_saved_defaults),
        TEST_CASE(a_custom_volume_tap_keeps_the_play_chord_skip),
        TEST_CASE(a_custom_play_tap_does_not_run_after_a_chord),
        TEST_CASE(a_mapped_a_b_repeat_sets_a_then_b_then_clears),
        TEST_CASE(a_mapped_play_hold_waits_for_release_so_a_chord_still_skips),
        TEST_CASE(a_mapped_a_b_refused_b_says_why),
        TEST_CASE(a_lone_a_mark_stays_through_a_scrub_and_a_loop_declines_it),
        TEST_CASE(a_play_hold_begun_on_a_list_follows_the_screen_going_dark),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
