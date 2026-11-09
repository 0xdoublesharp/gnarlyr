#include <stdio.h>
#include <string.h>

#include "check.h"
#include "fake_hal.h"
#include "favorite_playlist.h"
#include "favorites_store.h"
#include "fixture_library.h"
#include "hal/audio.h"
#include "hal/storage.h"
#include "hal/system.h"
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
    expect_playlist("#EXTM3U\n#DOUBLESHARP-START:300000\nAudiobooks/Zebra Tale.m4b\n"
                    "#DOUBLESHARP-START:0\nAudiobooks/Zebra Tale.m4b\n");
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
    expect_playlist("#EXTM3U\n#DOUBLESHARP-START:0\nAudiobooks/Zebra Tale.m4b\n"
                    "#DOUBLESHARP-START:900000\nAudiobooks/Zebra Tale.m4b\n");
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
    expect_playlist("#EXTM3U\r\nAudiobooks/Zebra Tale.m4b\n"
                    "#DOUBLESHARP-START:300000\nAudiobooks/Zebra Tale.m4b\n");
    spoken_library_close(&spoken);
}

static void a_hand_edited_cue_part_with_windows_line_ends_is_found(void)
{
    fake_hal_reset();
    SpokenLibrary spoken;
    Library *library = spoken_library_open(&spoken);
    static const char TEXT[] =
        "#EXTM3U\r\n#EXTINF:600,Noon\r\n#DOUBLESHARP-START:300000\r\nAudiobooks/Zebra Tale.m4b\r\n";
    char padded[400];
    memset(padded, ' ', sizeof padded);
    memcpy(padded, TEXT, sizeof TEXT - 1);
    fake_hal_add_file(FAVORITE_PLAYLIST_PATH, (const uint8_t *)padded, sizeof padded);
    CHECK_EQUAL(FAVORITE_PLAYLIST_PRESENT,
                favorite_playlist_add(library, spoken_library_chapter(library, 2)));
    spoken_library_close(&spoken);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(without_a_file_the_defaults_are_used),
        TEST_CASE(saved_settings_are_loaded_again),
        TEST_CASE(the_equalizer_curve_reaches_the_hal),
        TEST_CASE(the_equalizer_is_saved_and_loaded_again),
        TEST_CASE(unchanged_settings_are_not_written_again),
        TEST_CASE(settings_survive_a_change_of_music_volume),
        TEST_CASE(settings_from_the_music_volume_move_to_internal_storage),
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
