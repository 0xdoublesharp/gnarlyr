#include "check.h"
#include "core/hash.h"
#include "fake_hal.h"
#include "favorites_store.h"
#include "fixture_library.h"
#include "hal/audio.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "settings_store.h"

enum {
    BRIGHT = 9,
    CHOSEN_ALBUM = 2,
    OTHER_ALBUM = 1
};

static FixtureLibrary fixture;
static SettingsStore settings;
static FavoritesStore favorites;

static void without_a_file_the_defaults_are_used(void)
{
    fake_hal_reset();
    settings_store_load(&settings);
    Settings defaults = settings_defaults();
    CHECK_EQUAL(defaults.brightness, settings.settings.brightness);
    CHECK_EQUAL(defaults.volume, settings.settings.volume);
}

static void saved_settings_are_loaded_again(void)
{
    fake_hal_reset();
    settings_store_load(&settings);
    settings.settings.brightness = BRIGHT;
    settings.settings.gain = GAIN_HIGH;
    CHECK(settings_store_save(&settings));
    settings_store_load(&settings);
    CHECK_EQUAL(BRIGHT, settings.settings.brightness);
    CHECK_EQUAL(GAIN_HIGH, settings.settings.gain);
}

static void unchanged_settings_are_not_written_again(void)
{
    fake_hal_reset();
    settings_store_load(&settings);
    CHECK(settings_store_save(&settings));
    int writes = fake_hal_write_count();
    CHECK(settings_store_save(&settings));
    CHECK_EQUAL(writes, fake_hal_write_count());
}

static void settings_survive_a_change_of_music_volume(void)
{
    fake_hal_reset();
    settings_store_load(&settings);
    settings.settings.brightness = BRIGHT;
    CHECK(settings_store_save(&settings));
    fake_hal_replace_music_volume();
    settings_store_load(&settings);
    CHECK_EQUAL(BRIGHT, settings.settings.brightness);
    settings.settings.gain = GAIN_HIGH;
    CHECK(settings_store_save(&settings));
    fake_hal_replace_music_volume();
    settings_store_load(&settings);
    CHECK_EQUAL(BRIGHT, settings.settings.brightness);
    CHECK_EQUAL(GAIN_HIGH, settings.settings.gain);
}

static void settings_from_the_music_volume_move_to_internal_storage(void)
{
    uint8_t old[SETTINGS_FILE_SIZE];
    Settings written = settings_defaults();
    written.brightness = BRIGHT;
    settings_encode(&written, old);
    fake_hal_reset();
    fake_hal_add_file(SETTINGS_STORE_PATH, old, sizeof old);
    settings_store_load(&settings);
    CHECK_EQUAL(BRIGHT, settings.settings.brightness);
    CHECK(settings_store_save(&settings));
    fake_hal_replace_music_volume();
    settings_store_load(&settings);
    CHECK_EQUAL(BRIGHT, settings.settings.brightness);
}

static void internal_settings_win_over_the_music_volume_copy(void)
{
    uint8_t old[SETTINGS_FILE_SIZE];
    Settings written = settings_defaults();
    written.brightness = BRIGHT;
    settings_encode(&written, old);
    fake_hal_reset();
    settings_store_load(&settings);
    settings.settings.brightness = BRIGHT - 1;
    CHECK(settings_store_save(&settings));
    fake_hal_add_file(SETTINGS_STORE_PATH, old, sizeof old);
    settings_store_load(&settings);
    CHECK_EQUAL(BRIGHT - 1, settings.settings.brightness);
}

static void a_corrupted_settings_file_falls_back_to_the_defaults(void)
{
    static const uint8_t garbage[SETTINGS_FILE_SIZE] = {'E', 'M', 'S', 'T', 9};
    fake_hal_reset();
    fake_hal_add_file(SETTINGS_STORE_PATH, garbage, sizeof garbage);
    settings_store_load(&settings);
    CHECK_EQUAL(settings_defaults().brightness, settings.settings.brightness);
}

static void hardware_settings_reach_the_hal(void)
{
    fake_hal_reset();
    Settings chosen = settings_defaults();
    chosen.brightness = BRIGHT;
    chosen.gain = GAIN_HIGH;
    chosen.output = OUTPUT_BALANCED;
    chosen.usb_mode = USB_MODE_DAC;
    settings_store_apply_hardware(&chosen);
    CHECK_EQUAL(BRIGHT, fake_hal_display_brightness());
    CHECK_EQUAL(HAL_AUDIO_GAIN_HIGH, fake_hal_audio_gain());
    CHECK_EQUAL(HAL_AUDIO_OUTPUT_BALANCED, fake_hal_audio_output());
    CHECK_EQUAL(HAL_USB_MODE_DAC, fake_hal_usb_mode());
    CHECK_EQUAL(SETTINGS_DEFAULT_DAC_VOLUME, fake_hal_dac_volume());
}

static void toggling_an_album_adds_it_and_saves(void)
{
    fake_hal_reset();
    favorites_store_load(&favorites, fixture_library_open(&fixture));
    FavoriteResult result = favorites_store_toggle_album(&favorites, CHOSEN_ALBUM);
    CHECK_EQUAL(FAVORITE_ADDED, result.change);
    CHECK(result.saved);
    CHECK(favorites_store_contains_album(&favorites, CHOSEN_ALBUM));
    CHECK(!favorites_store_contains_album(&favorites, OTHER_ALBUM));
}

static void favorites_are_loaded_again(void)
{
    fake_hal_reset();
    Library *library = fixture_library_open(&fixture);
    favorites_store_load(&favorites, library);
    favorites_store_toggle_album(&favorites, CHOSEN_ALBUM);
    favorites_store_load(&favorites, library);
    CHECK(favorites_store_contains_album(&favorites, CHOSEN_ALBUM));
    CHECK_EQUAL(FAVORITE_REMOVED, favorites_store_toggle_album(&favorites, CHOSEN_ALBUM).change);
    favorites_store_load(&favorites, library);
    CHECK(!favorites_store_contains_album(&favorites, CHOSEN_ALBUM));
}

static void a_short_legacy_settings_file_is_migrated_without_overwriting_it(void)
{
    uint8_t old[24] = {'E', 'M', 'S', 'T', 4, 0, BRIGHT, 2, 0, 0, 0, 0, 0, 21, 2, 0, 37};
    uint32_t checksum = hash_update(HASH_START, old, 20);
    for (int index = 0; index < 4; index++) {
        old[20 + index] = (uint8_t)(checksum >> (8 * index));
    }
    fake_hal_reset();
    CHECK(hal_internal_file_write("/.echo/settings.bin", old, sizeof old));
    settings_store_load(&settings);
    CHECK_EQUAL(BRIGHT, settings.settings.brightness);
    CHECK_EQUAL(21, settings.settings.volume);
    CHECK_EQUAL(37, settings.settings.dac_volume);
    CHECK(settings_store_save(&settings));
    uint32_t size = 0;
    const uint8_t *legacy = fake_hal_internal_file_bytes("/.echo/settings.bin", &size);
    CHECK_EQUAL(sizeof old, size);
    for (unsigned index = 0; index < sizeof old; index++) {
        CHECK_EQUAL(old[index], legacy[index]);
    }
    settings_store_load(&settings);
    CHECK_EQUAL(BRIGHT, settings.settings.brightness);
    CHECK_EQUAL(37, settings.settings.dac_volume);
}

static void version_five_settings_migrate_without_overwriting_the_rollback_copy(void)
{
    fake_hal_reset();
    Settings original = settings_defaults();
    original.volume = 23;
    original.gain = GAIN_LOW;
    original.button_actions[PLAYBACK_MENU_TAP] = BUTTON_ACTION_NEXT;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&original, bytes);
    bytes[4] = 5;
    bytes[25] = 255;
    uint32_t checksum = hash_update(HASH_START, bytes, 28);
    for (int index = 0; index < 4; index++) {
        bytes[28 + index] = (uint8_t)(checksum >> (8 * index));
    }
    CHECK(hal_internal_file_write("/.echo/prefs5.bin", bytes, sizeof bytes));
    settings_store_load(&settings);
    CHECK_EQUAL(23, settings.settings.volume);
    CHECK_EQUAL(BUTTON_ACTION_NEXT, settings.settings.button_actions[PLAYBACK_MENU_TAP]);
    CHECK_EQUAL(APPEARANCE_NEON_NOIR, settings.settings.appearance);
    settings.settings.appearance = APPEARANCE_SUNSET;
    CHECK(settings_store_save(&settings));
    uint8_t preserved[SETTINGS_FILE_SIZE];
    CHECK(hal_internal_file_read("/.echo/prefs5.bin", preserved, sizeof preserved));
    for (unsigned index = 0; index < sizeof bytes; index++) {
        CHECK_EQUAL(bytes[index], preserved[index]);
    }
    settings_store_load(&settings);
    CHECK_EQUAL(23, settings.settings.volume);
    CHECK_EQUAL(APPEARANCE_SUNSET, settings.settings.appearance);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(without_a_file_the_defaults_are_used),
        TEST_CASE(version_five_settings_migrate_without_overwriting_the_rollback_copy),
        TEST_CASE(a_short_legacy_settings_file_is_migrated_without_overwriting_it),
        TEST_CASE(saved_settings_are_loaded_again),
        TEST_CASE(unchanged_settings_are_not_written_again),
        TEST_CASE(settings_survive_a_change_of_music_volume),
        TEST_CASE(settings_from_the_music_volume_move_to_internal_storage),
        TEST_CASE(internal_settings_win_over_the_music_volume_copy),
        TEST_CASE(a_corrupted_settings_file_falls_back_to_the_defaults),
        TEST_CASE(hardware_settings_reach_the_hal),
        TEST_CASE(toggling_an_album_adds_it_and_saves),
        TEST_CASE(favorites_are_loaded_again),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
