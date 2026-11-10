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

static void default_controls_still_pause_change_volume_and_open_up_next(void)
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
    CHECK(strcmp(app_screen_title(), "Up Next") == 0);
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
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK(strcmp(app_screen_title(), "Settings") == 0);
    tap(KEY_VOLUME_DOWN);
    tap(KEY_PLAY);
    CHECK(strcmp(app_screen_title(), "Controls") == 0);
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

static void configuring_one_rocker_key_keeps_the_other_keys_double_skip(void)
{
    Settings settings = settings_defaults();
    settings.skip_gesture = SKIP_GESTURE_DOUBLE_VOLUME;
    settings.button_actions[PLAYBACK_VOLUME_UP_TAP] = BUTTON_ACTION_VOLUME_UP;
    start_with(settings);
    uint16_t first = app_player_status().track;
    fake_hal_queue_headset_command(HAL_HEADSET_NEXT);
    wait_ms(40);
    CHECK(app_player_status().track != first);
    tap(KEY_VOLUME_DOWN);
    tap(KEY_VOLUME_DOWN);
    wait_ms(400);
    CHECK_EQUAL(first, app_player_status().track);
}

static void a_custom_rocker_keeps_the_default_play_multi_click_gesture(void)
{
    Settings settings = settings_defaults();
    settings.skip_gesture = SKIP_GESTURE_PLAY_CLICKS;
    settings.button_actions[PLAYBACK_VOLUME_UP_TAP] = BUTTON_ACTION_VOLUME_UP;
    start_with(settings);
    uint16_t first = app_player_status().track;
    int plays = fake_hal_audio_play_count();
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    wait_ms(400);
    CHECK(app_player_status().track != first);
    CHECK_EQUAL(plays + 1, fake_hal_audio_play_count());
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
}

static void configured_seek_round_trip(bool screen_off)
{
    Settings settings = settings_defaults();
    settings.button_actions[PLAYBACK_PLAY_TAP] = BUTTON_ACTION_SEEK;
    settings.button_actions[PLAYBACK_VOLUME_UP_TAP] = BUTTON_ACTION_NEXT;
    start_with(settings);
    fake_hal_audio_set_position(30000);
    wait_ms(40);
    uint16_t track = app_player_status().track;
    int plays = fake_hal_audio_play_count();
    int seeks = fake_hal_audio_seek_count();
    tap(KEY_PLAY);
    CHECK_EQUAL(seeks, fake_hal_audio_seek_count());
    if (screen_off) {
        tap(KEY_POWER);
        CHECK(!fake_hal_display_on());
    }
    tap(KEY_VOLUME_UP);
    CHECK(fake_hal_display_on());
    CHECK_EQUAL(track, app_player_status().track);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
    CHECK_EQUAL(seeks, fake_hal_audio_seek_count());
    tap(KEY_PLAY);
    CHECK_EQUAL(seeks + 1, fake_hal_audio_seek_count());
    CHECK_EQUAL(35000, fake_hal_audio_last_seek());
    CHECK_EQUAL(track, app_player_status().track);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
}

static void a_mapped_seek_owns_the_rocker_and_play_until_it_commits(void)
{
    configured_seek_round_trip(false);
    configured_seek_round_trip(true);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(default_controls_still_pause_change_volume_and_open_up_next),
        TEST_CASE(a_custom_play_tap_runs_once_without_also_pausing),
        TEST_CASE(a_custom_play_hold_runs_once_without_the_tap_action),
        TEST_CASE(holding_a_mapped_skip_does_not_skip_again_on_repeat_events),
        TEST_CASE(an_audio_mapping_works_with_the_screen_off_without_waking_it),
        TEST_CASE(a_navigation_mapping_wakes_the_screen_and_opens_its_destination),
        TEST_CASE(a_home_mapping_wakes_the_screen_before_showing_home),
        TEST_CASE(reset_buttons_restores_the_live_controls_and_saved_defaults),
        TEST_CASE(configuring_one_rocker_key_keeps_the_other_keys_double_skip),
        TEST_CASE(a_custom_rocker_keeps_the_default_play_multi_click_gesture),
        TEST_CASE(a_mapped_seek_owns_the_rocker_and_play_until_it_commits),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
