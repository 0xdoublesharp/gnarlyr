#include <string.h>

#include "app/app.h"
#include "app_internal.h"
#include "card_files.h"
#include "check.h"
#include "cover_cache.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "headphone_store.h"
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

static void open_panel(void)
{
    tap(KEY_MENU);
    CHECK(strcmp(app_screen_title(), "Quick Settings") == 0);
}

static void select_row(int row)
{
    for (int index = 0; index < row; index++) {
        tap(KEY_VOLUME_UP);
    }
}

static Settings saved(void)
{
    SettingsStore store;
    settings_store_load(&store);
    return store.settings;
}

static void menu_opens_the_panel_and_back_returns_without_restarting_audio(void)
{
    start_with(settings_defaults());
    int plays = fake_hal_audio_play_count();
    int releases = fake_hal_audio_release_count();
    open_panel();
    tap(KEY_BACK);
    CHECK(strcmp(app_screen_title(), "Now Playing") == 0);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
    CHECK_EQUAL(releases, fake_hal_audio_release_count());
}

static void shuffle_and_repeat_cycle_and_survive_reload(void)
{
    start_with(settings_defaults());
    open_panel();
    for (int index = 1; index <= SHUFFLE_MODE_COUNT; index++) {
        tap(KEY_PLAY);
        CHECK_EQUAL(index % SHUFFLE_MODE_COUNT, app_player_status().shuffle);
        CHECK_EQUAL(index % SHUFFLE_MODE_COUNT, saved().shuffle);
    }
    select_row(1);
    for (int index = 1; index <= REPEAT_MODE_COUNT; index++) {
        tap(KEY_PLAY);
        CHECK_EQUAL(index % REPEAT_MODE_COUNT, app_player_status().repeat);
        CHECK_EQUAL(index % REPEAT_MODE_COUNT, saved().repeat);
    }
}

static void equalizer_changes_immediately_and_saves_without_reopening_the_track(void)
{
    start_with(settings_defaults());
    int plays = fake_hal_audio_play_count();
    open_panel();
    select_row(2);
    tap(KEY_PLAY);
    CHECK_EQUAL(EQUALIZER_RETRO, saved().equalizer);
    CHECK(fake_hal_audio_equalizer().band_count > 0);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
}

static void replay_gain_and_transitions_reuse_saved_settings(void)
{
    start_with(settings_defaults());
    open_panel();
    select_row(6);
    tap(KEY_PLAY);
    CHECK_EQUAL(TRACK_TRANSITION_FADE, saved().track_transition);
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK_EQUAL(REPLAY_GAIN_TRACK, saved().replay_gain_mode);
    tap(KEY_PLAY);
    CHECK_EQUAL(REPLAY_GAIN_ALBUM, saved().replay_gain_mode);
    tap(KEY_PLAY);
    CHECK_EQUAL(REPLAY_GAIN_AUTO, saved().replay_gain_mode);
    tap(KEY_PLAY);
    CHECK_EQUAL(REPLAY_GAIN_OFF, saved().replay_gain_mode);
}

static void playback_speed_steps_applies_and_wraps_to_the_slowest(void)
{
    start_with(settings_defaults());
    int plays = fake_hal_audio_play_count();
    open_panel();
    select_row(4);
    tap(KEY_PLAY);
    CHECK_EQUAL(SETTINGS_SPEED_NORMAL + 1, saved().playback_speed);
    CHECK_EQUAL(125, fake_hal_audio_speed());
    for (int step = SETTINGS_SPEED_NORMAL + 1; step < SETTINGS_SPEED_COUNT; step++) {
        tap(KEY_PLAY);
    }
    CHECK_EQUAL(0, saved().playback_speed);
    CHECK_EQUAL(50, fake_hal_audio_speed());
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
    CHECK(strcmp(app_screen_title(), "Quick Settings") == 0);
}

static void lyrics_and_up_next_open_and_back_returns_to_the_panel(void)
{
    start_with(settings_defaults());
    open_panel();
    select_row(8);
    tap(KEY_PLAY);
    CHECK(strcmp(app_screen_title(), "Lyrics") == 0);
    tap(KEY_BACK);
    CHECK(strcmp(app_screen_title(), "Quick Settings") == 0);
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK(strcmp(app_screen_title(), "Up Next") == 0);
    tap(KEY_BACK);
    CHECK(strcmp(app_screen_title(), "Quick Settings") == 0);
    tap(KEY_BACK);
    hold(KEY_MENU, 1000);
    CHECK(strcmp(app_screen_title(), "Lyrics") == 0);
}

static void the_panel_owns_volume_and_play_even_with_playback_remaps(void)
{
    Settings chosen = settings_defaults();
    chosen.button_actions[PLAYBACK_PLAY_TAP] = BUTTON_ACTION_NEXT;
    chosen.button_actions[PLAYBACK_VOLUME_UP_TAP] = BUTTON_ACTION_VOLUME_UP;
    start_with(chosen);
    int plays = fake_hal_audio_play_count();
    int volume = fake_hal_audio_volume();
    open_panel();
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK_EQUAL(REPEAT_ONE, saved().repeat);
    CHECK_EQUAL(volume, fake_hal_audio_volume());
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(menu_opens_the_panel_and_back_returns_without_restarting_audio),
        TEST_CASE(shuffle_and_repeat_cycle_and_survive_reload),
        TEST_CASE(equalizer_changes_immediately_and_saves_without_reopening_the_track),
        TEST_CASE(replay_gain_and_transitions_reuse_saved_settings),
        TEST_CASE(playback_speed_steps_applies_and_wraps_to_the_slowest),
        TEST_CASE(lyrics_and_up_next_open_and_back_returns_to_the_panel),
        TEST_CASE(the_panel_owns_volume_and_play_even_with_playback_remaps),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
