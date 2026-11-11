#include <string.h>

#include "app/app.h"
#include "app_internal.h"
#include "bluetooth_store.h"
#include "card_files.h"
#include "check.h"
#include "cover_cache.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "library_store.h"
#include "settings/settings.h"
#include "settings_store.h"
#include "ui/toast.h"

enum {
    MUSIC_ALBUMS_INDEX = 4,
    LOUD = 50,
    QUIET = 10,
    BELOW_SAFE = 20,
    START_MS = 1000
};

static const HalBluetoothDevice BUDS = {.address = {1, 2, 3, 4, 5, 6}, .name = "Buds"};

static uint32_t now_ms;
static HalAudioJack boot_jack;

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

static void boot_with(Settings chosen, bool bluetooth)
{
    fake_hal_reset();
    fake_hal_audio_set_jack(boot_jack);
    add_player_files();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&chosen, bytes);
    CHECK(hal_internal_file_write(SETTINGS_STORE_PATH, bytes, sizeof bytes));
    BluetoothSaved saved = {.enabled = bluetooth, .count = bluetooth ? 1 : 0};
    saved.devices[0] = BUDS;
    bluetooth_store_save(&saved);
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
}

static Settings with_volume(uint8_t volume)
{
    Settings settings = settings_defaults();
    settings.volume = volume;
    return settings;
}

static void start_playing(void)
{
    wait_ms(1200);
    tap(KEY_PLAY);
    for (int row = 0; row < MUSIC_ALBUMS_INDEX; row++) {
        tap(KEY_VOLUME_UP);
    }
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    wait_ms(400);
    CHECK(strcmp(app_screen_title(), "Now Playing") == 0);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
}

static void raise_volume(int steps)
{
    for (int index = 0; index < steps; index++) {
        tap(KEY_VOLUME_UP);
    }
    wait_ms(TOAST_DURATION_MS + 100);
}

static bool toast_is(const char *text)
{
    const char *shown = app_toast_text();
    return shown != NULL && strcmp(shown, text) == 0;
}

static bool toast_shown_within(const char *text, uint32_t duration)
{
    for (uint32_t elapsed = 0; elapsed <= duration; elapsed += 50) {
        if (toast_is(text)) {
            return true;
        }
        wait_ms(50);
    }
    return false;
}

static void a_loud_volume_is_lowered_at_power_on_with_a_toast(void)
{
    boot_with(with_volume(LOUD), false);
    CHECK_EQUAL(SETTINGS_DEFAULT_SAFE_START_VOLUME, fake_hal_audio_volume());
    CHECK(toast_shown_within("Volume lowered to 24", 2 * TOAST_DURATION_MS));
}

static void a_quiet_volume_is_never_raised_at_power_on(void)
{
    boot_with(with_volume(QUIET), false);
    CHECK(!toast_shown_within("Volume lowered to 10", 2 * TOAST_DURATION_MS));
    CHECK_EQUAL(QUIET, fake_hal_audio_volume());
}

static void safe_start_volume_off_keeps_a_loud_volume(void)
{
    Settings settings = with_volume(LOUD);
    settings.safe_start_volume = false;
    boot_with(settings, false);
    CHECK(!toast_shown_within("Volume lowered to 24", 2 * TOAST_DURATION_MS));
    CHECK_EQUAL(LOUD, fake_hal_audio_volume());
}

static void the_chosen_level_is_the_cap(void)
{
    Settings settings = with_volume(LOUD);
    settings.safe_start_volume_level = 36;
    boot_with(settings, false);
    CHECK_EQUAL(36, fake_hal_audio_volume());
}

static void the_lowered_volume_is_saved(void)
{
    boot_with(with_volume(LOUD), false);
    app_stop();
    SettingsStore saved;
    settings_store_load(&saved);
    CHECK_EQUAL(SETTINGS_DEFAULT_SAFE_START_VOLUME, saved.settings.volume);
}

static void playing_with_a_loud_volume(void)
{
    boot_with(with_volume(BELOW_SAFE), false);
    start_playing();
    raise_volume(20);
    CHECK_EQUAL(BELOW_SAFE + 20, fake_hal_audio_volume());
}

static void plugging_in_headphones_lowers_a_loud_volume(void)
{
    playing_with_a_loud_volume();
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_SINGLE_ENDED);
    step();
    CHECK_EQUAL(SETTINGS_DEFAULT_SAFE_START_VOLUME, fake_hal_audio_volume());
    step();
    CHECK(toast_shown_within("Volume lowered to 24", 100));
}

static void raising_the_volume_after_the_cap_is_left_alone(void)
{
    playing_with_a_loud_volume();
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_SINGLE_ENDED);
    step();
    raise_volume(10);
    wait_ms(5000);
    CHECK_EQUAL(SETTINGS_DEFAULT_SAFE_START_VOLUME + 10, fake_hal_audio_volume());
}

static void plugging_in_the_other_jack_lowers_the_volume_again(void)
{
    playing_with_a_loud_volume();
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_SINGLE_ENDED);
    step();
    raise_volume(10);
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_BALANCED);
    step();
    CHECK_EQUAL(SETTINGS_DEFAULT_SAFE_START_VOLUME, fake_hal_audio_volume());
}

static void unplugging_headphones_leaves_the_volume(void)
{
    boot_with(with_volume(BELOW_SAFE), false);
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_SINGLE_ENDED);
    step();
    start_playing();
    raise_volume(20);
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_NONE);
    step();
    CHECK_EQUAL(BELOW_SAFE + 20, fake_hal_audio_volume());
}

static void headphones_already_plugged_in_at_power_on_are_not_a_second_plug(void)
{
    boot_jack = HAL_AUDIO_JACK_BALANCED;
    boot_with(with_volume(BELOW_SAFE), false);
    boot_jack = HAL_AUDIO_JACK_NONE;
    start_playing();
    raise_volume(20);
    wait_ms(1000);
    CHECK_EQUAL(BELOW_SAFE + 20, fake_hal_audio_volume());
}

static void bluetooth_headphones_connecting_lower_a_loud_volume(void)
{
    boot_with(with_volume(BELOW_SAFE), true);
    start_playing();
    raise_volume(20);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTED;
    fake_hal_bluetooth()->connected = BUDS;
    step();
    CHECK_EQUAL(SETTINGS_DEFAULT_SAFE_START_VOLUME, fake_hal_audio_volume());
}

static void the_volume_toast_follows_the_bluetooth_toast(void)
{
    boot_with(with_volume(BELOW_SAFE), true);
    start_playing();
    raise_volume(20);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTED;
    fake_hal_bluetooth()->connected = BUDS;
    step();
    CHECK(toast_is("Connected to Buds"));
    wait_ms(TOAST_DURATION_MS + 50);
    wait_ms(10);
    CHECK(toast_is("Volume lowered to 24"));
}

static void the_volume_toast_is_dropped_once_the_volume_changed(void)
{
    boot_with(with_volume(BELOW_SAFE), true);
    start_playing();
    raise_volume(20);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTED;
    fake_hal_bluetooth()->connected = BUDS;
    step();
    tap(KEY_VOLUME_UP);
    wait_ms(TOAST_DURATION_MS + 50);
    wait_ms(10);
    CHECK(app_toast_text() == NULL);
}

static void turning_on_from_the_charging_screen_lowers_a_loud_volume(void)
{
    fake_hal_reset();
    add_player_files();
    Settings settings = with_volume(LOUD);
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK(hal_internal_file_write(SETTINGS_STORE_PATH, bytes, sizeof bytes));
    fake_hal_set_battery((HalBattery){
        .known = true, .percent = 64, .millivolts = 3900, .usb_power = true, .charging = true});
    fake_hal_set_usb_start(true);
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    CHECK(app_charging());
    CHECK_EQUAL(LOUD, fake_hal_audio_volume());
    hold(KEY_POWER, 2100);
    CHECK(!app_charging());
    CHECK_EQUAL(SETTINGS_DEFAULT_SAFE_START_VOLUME, fake_hal_audio_volume());
}

static void paused_for_a_minute(Settings settings)
{
    boot_with(settings, false);
    start_playing();
    fake_hal_audio_set_position(42000);
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    for (int second = 0; second < 61; second++) {
        wait_ms(1000);
    }
}

static void resuming_after_a_minute_jumps_back_three_seconds(void)
{
    paused_for_a_minute(settings_defaults());
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    uint32_t start = fake_hal_audio_play_start();
    CHECK(start >= 38950 && start <= 39000);
}

static void rewind_off_resumes_where_it_paused(void)
{
    Settings settings = settings_defaults();
    settings.rewind_on_resume = false;
    paused_for_a_minute(settings);
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK_EQUAL(42000, fake_hal_audio_play_start());
}

static void a_resume_after_power_off_starts_twenty_seconds_back(void)
{
    boot_with(settings_defaults(), false);
    start_playing();
    fake_hal_audio_set_position(42000);
    tap(KEY_PLAY);
    app_stop();
    Settings settings = settings_defaults();
    settings.start_screen = START_NOW_PLAYING;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK(hal_internal_file_write(SETTINGS_STORE_PATH, bytes, sizeof bytes));
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    wait_ms(1200);
    CHECK(strcmp(app_screen_title(), "Now Playing") == 0);
    CHECK_EQUAL(42000, app_player_status().position_ms);
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK_EQUAL(42000 - RESUME_REWIND_COLD_MUSIC_MS, fake_hal_audio_play_start());
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_loud_volume_is_lowered_at_power_on_with_a_toast),
        TEST_CASE(a_quiet_volume_is_never_raised_at_power_on),
        TEST_CASE(safe_start_volume_off_keeps_a_loud_volume),
        TEST_CASE(the_chosen_level_is_the_cap),
        TEST_CASE(the_lowered_volume_is_saved),
        TEST_CASE(plugging_in_headphones_lowers_a_loud_volume),
        TEST_CASE(raising_the_volume_after_the_cap_is_left_alone),
        TEST_CASE(plugging_in_the_other_jack_lowers_the_volume_again),
        TEST_CASE(unplugging_headphones_leaves_the_volume),
        TEST_CASE(headphones_already_plugged_in_at_power_on_are_not_a_second_plug),
        TEST_CASE(bluetooth_headphones_connecting_lower_a_loud_volume),
        TEST_CASE(the_volume_toast_follows_the_bluetooth_toast),
        TEST_CASE(the_volume_toast_is_dropped_once_the_volume_changed),
        TEST_CASE(turning_on_from_the_charging_screen_lowers_a_loud_volume),
        TEST_CASE(resuming_after_a_minute_jumps_back_three_seconds),
        TEST_CASE(rewind_off_resumes_where_it_paused),
        TEST_CASE(a_resume_after_power_off_starts_twenty_seconds_back),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
