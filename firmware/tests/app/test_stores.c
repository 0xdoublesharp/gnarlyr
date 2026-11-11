#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "default_theme.h"
#include "equalizer_presets.h"
#include "equalizer_ui.h"
#include "fake_hal.h"
#include "favorite_playlist.h"
#include "favorites_store.h"
#include "fixture_library.h"
#include "hal/audio.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "library/playlist_reader.h"
#include "settings_store.h"
#include "spoken_library.h"

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

static void clock_offsets_survive_reload_without_losing_other_settings(void)
{
    fake_hal_reset();
    settings_store_load(&settings);
    settings.settings.brightness = BRIGHT;
    CHECK(settings_store_set_clock_offset(&settings, -480));
    settings_store_load(&settings);
    CHECK_EQUAL(-480, settings.settings.clock_offset_minutes);
    CHECK_EQUAL(BRIGHT, settings.settings.brightness);
    CHECK(settings_store_set_clock_offset(&settings, 345));
    settings_store_load(&settings);
    CHECK_EQUAL(345, settings.settings.clock_offset_minutes);
    fake_hal_fail_file_writes(SETTINGS_STORE_PATH);
    CHECK(!settings_store_set_clock_offset(&settings, -60));
    CHECK_EQUAL(345, settings.settings.clock_offset_minutes);
    settings_store_load(&settings);
    CHECK_EQUAL(345, settings.settings.clock_offset_minutes);
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

static void a_version_fourteen_file_loads_and_is_saved_as_version_fifteen(void)
{
    uint8_t old[SETTINGS_FILE_SIZE];
    Settings written = settings_defaults();
    written.brightness = BRIGHT;
    written.clock_24h = false;
    settings_encode(&written, old);
    old[4] = 14;
    hash_store(old + 38, hash_update(HASH_START, old, 38));
    fake_hal_reset();
    fake_hal_add_file(SETTINGS_STORE_PATH, old, SETTINGS_V14_FILE_SIZE);
    settings_store_load(&settings);
    CHECK_EQUAL(BRIGHT, settings.settings.brightness);
    CHECK(settings.settings.clock_24h);
    CHECK(settings_store_save(&settings));
    uint32_t size = 0;
    const uint8_t *saved = fake_hal_internal_file_bytes(SETTINGS_STORE_PATH, &size);
    CHECK_EQUAL(SETTINGS_FILE_SIZE, size);
    CHECK_EQUAL(15, saved[4]);
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
    static const uint8_t garbage[SETTINGS_FILE_SIZE] = {'D', 'S', 'S', 'T', 9};
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

static void the_equalizer_curve_reaches_the_hal(void)
{
    fake_hal_reset();
    Settings chosen = settings_defaults();
    chosen.equalizer = EQUALIZER_CUSTOM;
    chosen.equalizer_gains[1] = -4;
    chosen.equalizer_gains[4] = 7;
    settings_store_apply_hardware(&chosen);
    HalAudioEqualizer curve = fake_hal_audio_equalizer();
    CHECK_EQUAL(2, curve.band_count);
    CHECK_EQUAL(62, curve.band_hz[0]);
    CHECK_EQUAL(-4, curve.gain_half_db[0]);
    CHECK_EQUAL(500, curve.band_hz[1]);
    CHECK_EQUAL(7, curve.gain_half_db[1]);
    CHECK_EQUAL(settings_equalizer_curve(&chosen).pre_gain, curve.pre_gain);
}

static void the_equalizer_is_saved_and_loaded_again(void)
{
    fake_hal_reset();
    settings_store_load(&settings);
    settings.settings.equalizer = EQUALIZER_CUSTOM;
    settings.settings.equalizer_gains[3] = -9;
    CHECK(settings_store_save(&settings));
    settings_store_load(&settings);
    CHECK_EQUAL(EQUALIZER_CUSTOM, settings.settings.equalizer);
    CHECK_EQUAL(-9, settings.settings.equalizer_gains[3]);
    CHECK_EQUAL(-1, settings.equalizer_slot);
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

static void track_path_of(const Library *library, uint16_t track, char *path)
{
    LibraryTrack record;
    CHECK(library_read_track(library, track, &record));
    CHECK(library_read_string(library, record.path, path, 260));
}

static const char *playlist_text(void)
{
    uint32_t size = 0;
    const char *bytes = (const char *)fake_hal_file_bytes(FAVORITE_PLAYLIST_PATH, &size);
    return bytes == NULL ? "" : bytes;
}

static void expect_playlist(const char *expected)
{
    CHECK(strncmp(playlist_text(), expected, strlen(expected)) == 0);
    CHECK(playlist_text()[strlen(expected)] == ' ');
}

static void adding_a_song_creates_the_playlist_with_a_header(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    Library *library = fixture_library_open(&fixture);
    char first[260];
    char expected[300];
    track_path_of(library, 0, first);
    CHECK_EQUAL(FAVORITE_PLAYLIST_ADDED, favorite_playlist_add(library, 0));
    snprintf(expected, sizeof expected, "#EXTM3U\n%s\n", first);
    expect_playlist(expected);
    CHECK_EQUAL(32 * 1024, fake_hal_reserved_size(FAVORITE_PLAYLIST_PATH));
}

static void adding_more_songs_appends_one_line_each(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    Library *library = fixture_library_open(&fixture);
    char first[260];
    char second[260];
    char expected[600];
    track_path_of(library, 0, first);
    track_path_of(library, 1, second);
    CHECK_EQUAL(FAVORITE_PLAYLIST_ADDED, favorite_playlist_add(library, 0));
    CHECK_EQUAL(FAVORITE_PLAYLIST_ADDED, favorite_playlist_add(library, 1));
    snprintf(expected, sizeof expected, "#EXTM3U\n%s\n%s\n", first, second);
    expect_playlist(expected);
}

static void a_song_already_in_the_playlist_is_not_added_again(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    Library *library = fixture_library_open(&fixture);
    char first[260];
    char expected[300];
    track_path_of(library, 0, first);
    CHECK_EQUAL(FAVORITE_PLAYLIST_ADDED, favorite_playlist_add(library, 0));
    CHECK_EQUAL(FAVORITE_PLAYLIST_PRESENT, favorite_playlist_add(library, 0));
    snprintf(expected, sizeof expected, "#EXTM3U\n%s\n", first);
    expect_playlist(expected);
}

static void a_hand_edited_playlist_is_read_and_extended(void)
{
    fake_hal_reset();
    Library *library = fixture_library_open(&fixture);
    char first[260];
    char second[260];
    char text[1100] = "#EXTM3U\r\n#EXTINF:1,x\r\n";
    char expected[1100];
    track_path_of(library, 0, first);
    track_path_of(library, 1, second);
    strcat(text, first);
    strcat(text, "\r\n");
    size_t length = strlen(text);
    memset(text + length, ' ', 200);
    fake_hal_add_file(FAVORITE_PLAYLIST_PATH, (const uint8_t *)text, (uint32_t)length + 200);
    CHECK_EQUAL(FAVORITE_PLAYLIST_PRESENT, favorite_playlist_add(library, 0));
    CHECK_EQUAL(FAVORITE_PLAYLIST_ADDED, favorite_playlist_add(library, 1));
    memcpy(expected, text, length - 2);
    snprintf(expected + length - 2, sizeof expected - length, "\n%s\n", second);
    CHECK(strncmp(playlist_text(), expected, strlen(expected)) == 0);
}

static void a_full_playlist_file_reports_failure(void)
{
    fake_hal_reset();
    Library *library = fixture_library_open(&fixture);
    fake_hal_add_file(FAVORITE_PLAYLIST_PATH, (const uint8_t *)"#EXTM3U\n", 8);
    CHECK_EQUAL(FAVORITE_PLAYLIST_FAILED, favorite_playlist_add(library, 0));
}

static void a_card_without_room_reports_failure(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_NO_SPACE);
    Library *library = fixture_library_open(&fixture);
    CHECK_EQUAL(FAVORITE_PLAYLIST_FAILED, favorite_playlist_add(library, 0));
}

static void adding_a_cue_part_writes_its_start(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    SpokenLibrary spoken;
    Library *library = spoken_library_open(&spoken);
    CHECK_EQUAL(FAVORITE_PLAYLIST_ADDED,
                favorite_playlist_add(library, spoken_library_chapter(library, 2)));
    CHECK_EQUAL(FAVORITE_PLAYLIST_ADDED,
                favorite_playlist_add(library, spoken_library_chapter(library, 1)));
    expect_playlist("#EXTM3U\n" PLAYLIST_START_DIRECTIVE
                    "300000\nAudiobooks/Zebra Tale.m4b\n" PLAYLIST_START_DIRECTIVE
                    "0\nAudiobooks/Zebra Tale.m4b\n");
    spoken_library_close(&spoken);
}

static void each_cue_part_is_present_only_once(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    SpokenLibrary spoken;
    Library *library = spoken_library_open(&spoken);
    uint16_t first = spoken_library_chapter(library, 1);
    uint16_t last = spoken_library_chapter(library, 3);
    CHECK_EQUAL(FAVORITE_PLAYLIST_ADDED, favorite_playlist_add(library, first));
    CHECK_EQUAL(FAVORITE_PLAYLIST_PRESENT, favorite_playlist_add(library, first));
    CHECK_EQUAL(FAVORITE_PLAYLIST_ADDED, favorite_playlist_add(library, last));
    CHECK_EQUAL(FAVORITE_PLAYLIST_PRESENT, favorite_playlist_add(library, last));
    CHECK_EQUAL(FAVORITE_PLAYLIST_PRESENT, favorite_playlist_add(library, first));
    expect_playlist("#EXTM3U\n" PLAYLIST_START_DIRECTIVE
                    "0\nAudiobooks/Zebra Tale.m4b\n" PLAYLIST_START_DIRECTIVE
                    "900000\nAudiobooks/Zebra Tale.m4b\n");
    spoken_library_close(&spoken);
}

static void a_plain_line_for_the_file_does_not_stand_for_a_cue_part(void)
{
    fake_hal_reset();
    SpokenLibrary spoken;
    Library *library = spoken_library_open(&spoken);
    static const char TEXT[] = "#EXTM3U\r\nAudiobooks/Zebra Tale.m4b\r\n";
    char padded[400];
    memset(padded, ' ', sizeof padded);
    memcpy(padded, TEXT, sizeof TEXT - 1);
    fake_hal_add_file(FAVORITE_PLAYLIST_PATH, (const uint8_t *)padded, sizeof padded);
    CHECK_EQUAL(FAVORITE_PLAYLIST_ADDED,
                favorite_playlist_add(library, spoken_library_chapter(library, 2)));
    CHECK_EQUAL(FAVORITE_PLAYLIST_PRESENT,
                favorite_playlist_add(library, spoken_library_chapter(library, 2)));
    expect_playlist("#EXTM3U\r\nAudiobooks/Zebra Tale.m4b\n" PLAYLIST_START_DIRECTIVE
                    "300000\nAudiobooks/Zebra Tale.m4b\n");
    spoken_library_close(&spoken);
}

static void a_hand_edited_cue_part_with_windows_line_ends_is_found(void)
{
    fake_hal_reset();
    SpokenLibrary spoken;
    Library *library = spoken_library_open(&spoken);
    static const char TEXT[] = "#EXTM3U\r\n#EXTINF:600,Noon\r\n" PLAYLIST_START_DIRECTIVE
                               "300000\r\nAudiobooks/Zebra Tale.m4b\r\n";
    char padded[400];
    memset(padded, ' ', sizeof padded);
    memcpy(padded, TEXT, sizeof TEXT - 1);
    fake_hal_add_file(FAVORITE_PLAYLIST_PATH, (const uint8_t *)padded, sizeof padded);
    CHECK_EQUAL(FAVORITE_PLAYLIST_PRESENT,
                favorite_playlist_add(library, spoken_library_chapter(library, 2)));
    spoken_library_close(&spoken);
}

static void theme_and_button_choices_survive_a_restart_and_card_change(void)
{
    fake_hal_reset();
    settings_store_load(&settings);
    settings.settings.appearance = APPEARANCE_SEA_GLASS;
    settings.settings.button_actions[PLAYBACK_MENU_TAP] = BUTTON_ACTION_UP_NEXT;
    CHECK(settings_store_save(&settings));
    fake_hal_replace_music_volume();
    settings_store_load(&settings);
    CHECK_EQUAL(APPEARANCE_SEA_GLASS, settings.settings.appearance);
    CHECK_EQUAL(BUTTON_ACTION_UP_NEXT,
                settings_button_action(&settings.settings, PLAYBACK_MENU_TAP));
    CHECK_EQUAL(BUTTON_ACTION_DEFAULT,
                settings_button_action(&settings.settings, PLAYBACK_PLAY_HOLD));
    int writes = fake_hal_write_count();
    CHECK(settings_store_save(&settings));
    CHECK_EQUAL(writes, fake_hal_write_count());
}

static void a_corrupt_button_map_keeps_other_settings_and_safe_controls(void)
{
    fake_hal_reset();
    settings_store_load(&settings);
    settings.settings.brightness = BRIGHT;
    settings.settings.button_actions[PLAYBACK_PLAY_TAP] = BUTTON_ACTION_NONE;
    CHECK(settings_store_save(&settings));
    uint8_t damaged[SETTINGS_BUTTONS_FILE_SIZE] = {0};
    CHECK(hal_internal_file_write(SETTINGS_BUTTONS_PATH, damaged, sizeof damaged));
    settings_store_load(&settings);
    CHECK_EQUAL(BRIGHT, settings.settings.brightness);
    CHECK_EQUAL(BUTTON_ACTION_DEFAULT,
                settings_button_action(&settings.settings, PLAYBACK_PLAY_TAP));
}

static void normalization_settings_restore_to_the_player_from_a_full_record(void)
{
    fake_hal_reset();
    settings_store_load(&settings);
    settings.settings.replay_gain_mode = REPLAY_GAIN_ALBUM;
    settings.settings.prevent_clipping = false;
    CHECK(settings_store_save(&settings));
    settings_store_load(&settings);
    Player player;
    player_init(&player, fixture_library_open(&fixture));
    settings_store_restore_playback(&settings, &player, 1);
    CHECK_EQUAL(REPLAY_GAIN_ALBUM, player.replay_gain_mode);
    CHECK(!player.prevent_clipping);
    uint32_t size = 0;
    CHECK(fake_hal_internal_file_bytes(SETTINGS_STORE_PATH, &size) != NULL);
    CHECK_EQUAL(SETTINGS_FILE_SIZE, size);
}

static void matching_graphic_and_parametric_slots_survive_restart(void)
{
    fake_hal_reset();
    SettingsStore store = {.settings = settings_defaults()};
    EqualizerSlots slots = {0};
    store.settings.equalizer = EQUALIZER_CUSTOM;
    store.settings.equalizer_gains[5] = 3;
    equalizer_slots_put(&slots, 2, store.settings.equalizer_gains);
    uint8_t bytes[EQUALIZER_SLOTS_FILE_SIZE];
    equalizer_slots_encode(&slots, bytes);
    CHECK(hal_internal_file_write(EQUALIZER_SLOTS_PATH, bytes, sizeof bytes));
    CHECK(settings_store_save(&store));
    settings_store_load(&store);
    CHECK_EQUAL(2, store.equalizer_slot);
    CHECK_EQUAL(3, store.settings.equalizer_gains[5]);
    CHECK_EQUAL(HAL_RESERVE_CREATED, hal_internal_file_reserve(PARAMETRIC_STORE_PATH, 4096));
    ParametricStore file = {0};
    parametric_store_read(&file, NULL);
    ParametricEq eq = {
        .count = 1, .preamp = -62, .filters = {{105, 70, -23, PARAMETRIC_PEAK, true}}};
    ParametricBank bank = {.custom = eq, .used = 1};
    bank.slots[0] = eq;
    CHECK(parametric_store_save(&file, &bank));
    store.settings.equalizer = EQUALIZER_PARAMETRIC;
    store.settings.parametric = eq;
    CHECK(settings_store_save(&store));
    settings_store_load(&store);
    CHECK_EQUAL(0, store.equalizer_slot);
    CHECK(parametric_same(&eq, &store.settings.parametric));
    store.settings.equalizer = EQUALIZER_OFF;
    CHECK(settings_store_save(&store));
    settings_store_load(&store);
    EqualizerUi ui;
    Player player = {0};
    Toast toast = {0};
    equalizer_ui_init(&ui, &store, &player, &toast, &BOARD_THEME);
    CHECK(parametric_same(&eq, &store.settings.parametric));
    CHECK_EQUAL(EQUALIZER_OFF, store.settings.equalizer);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(matching_graphic_and_parametric_slots_survive_restart),
        TEST_CASE(without_a_file_the_defaults_are_used),
        TEST_CASE(saved_settings_are_loaded_again),
        TEST_CASE(normalization_settings_restore_to_the_player_from_a_full_record),
        TEST_CASE(theme_and_button_choices_survive_a_restart_and_card_change),
        TEST_CASE(a_corrupt_button_map_keeps_other_settings_and_safe_controls),
        TEST_CASE(the_equalizer_curve_reaches_the_hal),
        TEST_CASE(the_equalizer_is_saved_and_loaded_again),
        TEST_CASE(clock_offsets_survive_reload_without_losing_other_settings),
        TEST_CASE(unchanged_settings_are_not_written_again),
        TEST_CASE(settings_survive_a_change_of_music_volume),
        TEST_CASE(settings_from_the_music_volume_move_to_internal_storage),
        TEST_CASE(a_version_fourteen_file_loads_and_is_saved_as_version_fifteen),
        TEST_CASE(internal_settings_win_over_the_music_volume_copy),
        TEST_CASE(a_corrupted_settings_file_falls_back_to_the_defaults),
        TEST_CASE(hardware_settings_reach_the_hal),
        TEST_CASE(toggling_an_album_adds_it_and_saves),
        TEST_CASE(favorites_are_loaded_again),
        TEST_CASE(adding_a_song_creates_the_playlist_with_a_header),
        TEST_CASE(adding_more_songs_appends_one_line_each),
        TEST_CASE(a_song_already_in_the_playlist_is_not_added_again),
        TEST_CASE(a_hand_edited_playlist_is_read_and_extended),
        TEST_CASE(a_full_playlist_file_reports_failure),
        TEST_CASE(a_card_without_room_reports_failure),
        TEST_CASE(adding_a_cue_part_writes_its_start),
        TEST_CASE(each_cue_part_is_present_only_once),
        TEST_CASE(a_plain_line_for_the_file_does_not_stand_for_a_cue_part),
        TEST_CASE(a_hand_edited_cue_part_with_windows_line_ends_is_found),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
