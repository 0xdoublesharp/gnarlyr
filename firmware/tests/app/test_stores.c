#include <stdio.h>
#include <string.h>

#include "bluetooth_store.h"
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
#include "music_check.h"
#include "music_record.h"
#include "scan_guard.h"
#include "settings_screens.h"
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

static BluetoothSaved load_bluetooth(void)
{
    BluetoothSaved saved;
    bluetooth_store_load(&saved);
    return saved;
}

static void bluetooth_records_bound_counts_and_terminate_names(void)
{
    fake_hal_reset();
    BluetoothSaved saved = {
        .enabled = true, .on_bluetooth = true, .count = BLUETOOTH_STORE_DEVICES + 2};
    for (uint32_t index = 0; index < BLUETOOTH_STORE_DEVICES; index++) {
        saved.devices[index].address[0] = (uint8_t)(index + 1);
        memset(saved.devices[index].name, 'A' + (int)index, sizeof saved.devices[index].name);
    }
    bluetooth_store_save(&saved);
    BluetoothSaved loaded = load_bluetooth();
    CHECK(loaded.enabled);
    CHECK(loaded.on_bluetooth);
    CHECK_EQUAL(BLUETOOTH_STORE_DEVICES, loaded.count);
    for (uint32_t index = 0; index < BLUETOOTH_STORE_DEVICES; index++) {
        CHECK_EQUAL(index + 1, loaded.devices[index].address[0]);
        CHECK_EQUAL(0, loaded.devices[index].name[HAL_BLUETOOTH_NAME_CAPACITY - 1]);
    }
}

static void malformed_bluetooth_records_restore_nothing(void)
{
    fake_hal_reset();
    BluetoothSaved saved = {.enabled = true, .on_bluetooth = true, .count = 1};
    saved.devices[0].address[0] = 7;
    memcpy(saved.devices[0].name, "Buds", 5);
    bluetooth_store_save(&saved);
    uint32_t size = 0;
    const uint8_t *stored = fake_hal_file_bytes(BLUETOOTH_STORE_PATH, &size);
    uint8_t record[512];
    CHECK(stored != NULL);
    CHECK(size < sizeof record);
    memcpy(record, stored, size);
    const uint32_t lengths[] = {0, 1, 2, 3, size - 2};
    for (size_t index = 0; index < sizeof lengths / sizeof lengths[0]; index++) {
        fake_hal_reset();
        fake_hal_add_file(BLUETOOTH_STORE_PATH, record, lengths[index]);
        BluetoothSaved loaded = load_bluetooth();
        CHECK(!loaded.enabled);
        CHECK(!loaded.on_bluetooth);
        CHECK_EQUAL(0, loaded.count);
    }
    for (uint32_t field = 0; field < 2; field++) {
        uint8_t damaged[512];
        memcpy(damaged, record, size);
        damaged[field == 0 ? 0 : 2] ^= 0x7F;
        fake_hal_reset();
        fake_hal_add_file(BLUETOOTH_STORE_PATH, damaged, size);
        BluetoothSaved loaded = load_bluetooth();
        CHECK(!loaded.enabled);
        CHECK(!loaded.on_bluetooth);
        CHECK_EQUAL(0, loaded.count);
    }
}

static MusicRecord combined_record(uint32_t internal_entries, MusicDriveState state)
{
    return (MusicRecord){
        .card = {.complete = true, .first = 1, .second = 2, .entries = 3},
        .internal = {.complete = true, .first = 4, .second = 5, .entries = internal_entries},
        .internal_state = state};
}

static void a_record_matches_only_when_both_drives_match(void)
{
    MusicRecord stored = combined_record(6, MUSIC_DRIVE_KNOWN);
    MusicRecord same = combined_record(6, MUSIC_DRIVE_KNOWN);
    same.generation = 9;
    CHECK(music_record_same_music(&stored, &same, 0));
    MusicRecord internal_changed = combined_record(7, MUSIC_DRIVE_KNOWN);
    CHECK(!music_record_same_music(&stored, &internal_changed, 0));
    MusicRecord card_only = combined_record(6, MUSIC_DRIVE_NONE);
    CHECK(!music_record_same_music(&stored, &card_only, 0));
    MusicRecord incomplete = same;
    incomplete.internal.complete = false;
    CHECK(!music_record_same_music(&stored, &incomplete, 0));
}

static void an_unknown_internal_half_is_accepted_only_without_internal_songs(void)
{
    MusicRecord stored = combined_record(0, MUSIC_DRIVE_UNKNOWN);
    MusicRecord walked = combined_record(4, MUSIC_DRIVE_KNOWN);
    CHECK(music_record_same_music(&stored, &walked, 0));
    CHECK(!music_record_same_music(&stored, &walked, 1));
}

static void an_old_record_reads_as_missing_and_the_generation_moves_with_bumps(void)
{
    fake_hal_reset();
    fake_hal_add_file(MUSIC_CHECK_PATH, NULL, 1024);
    uint8_t old[32] = {'D', 'S', 'S', 'C', 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0};
    CHECK(hal_file_write_at(MUSIC_CHECK_PATH, 0, old, sizeof old));
    MusicRecord read;
    CHECK(!music_record_read(&read));
    CHECK(!music_check_generation_moved());
    CHECK(hal_internal_file_reserve(MUSIC_GENERATION_PATH, 512) == HAL_RESERVE_CREATED);
    CHECK_EQUAL(0, music_check_generation());
    MusicRecord written = combined_record(6, MUSIC_DRIVE_KNOWN);
    written.generation = music_check_generation();
    music_record_write(&written);
    CHECK(!music_check_generation_moved());
    music_check_bump_generation();
    CHECK_EQUAL(1, music_check_generation());
    CHECK(music_check_generation_moved());
}

static void music_check_records_survive_versions_and_scan_interruptions(void)
{
    fake_hal_reset();
    fake_hal_add_file(MUSIC_CHECK_PATH, NULL, 1024);
    fake_hal_set_version("one");
    MusicRecord written = {.card = {.complete = true, .first = 1, .second = 2, .entries = 3},
                           .internal = {.complete = true, .first = 4, .second = 5, .entries = 6},
                           .internal_state = MUSIC_DRIVE_KNOWN,
                           .generation = 7};
    music_record_write(&written);
    MusicRecord read;
    CHECK(music_record_read(&read));
    CHECK(music_record_same_music(&read, &written, 0));
    CHECK_EQUAL(7, read.generation);
    CHECK(!music_check_firmware_is_new());
    fake_hal_set_version("two");
    CHECK(music_check_firmware_is_new());
    music_record_write(&written);
    CHECK(!music_check_firmware_is_new());
    CHECK(music_record_read(&read));
    CHECK(music_record_same_music(&read, &written, 0));
    ScanGuard guard = {.magic = SCAN_GUARD_MAGIC, .running = 1, .attempts = 3, .covers_pending = 1};
    CHECK(hal_file_write_at(MUSIC_CHECK_PATH, SCAN_GUARD_OFFSET, &guard, SCAN_GUARD_BYTES));
    CHECK_EQUAL(3, music_check_interrupted_scans());
    CHECK(music_check_covers_pending());
    CHECK(!music_record_settled());
    guard.running = 0;
    guard.attempts = 1;
    guard.covers_pending = 0;
    CHECK(hal_file_write_at(MUSIC_CHECK_PATH, SCAN_GUARD_OFFSET, &guard, SCAN_GUARD_BYTES));
    CHECK_EQUAL(0, music_check_interrupted_scans());
    CHECK(!music_check_covers_pending());
    CHECK(music_record_settled());
    guard.magic ^= 1;
    CHECK(hal_file_write_at(MUSIC_CHECK_PATH, SCAN_GUARD_OFFSET, &guard, SCAN_GUARD_BYTES));
    CHECK_EQUAL(0, music_check_interrupted_scans());
    CHECK(!music_check_covers_pending());
    CHECK(music_record_settled());
}

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

static void a_settings_file_on_the_music_volume_is_ignored(void)
{
    uint8_t bytes[SETTINGS_FILE_SIZE];
    Settings written = settings_defaults();
    written.brightness = BRIGHT;
    settings_encode(&written, bytes);
    fake_hal_reset();
    fake_hal_add_file(SETTINGS_STORE_PATH, bytes, sizeof bytes);
    settings_store_load(&settings);
    CHECK_EQUAL(settings_defaults().brightness, settings.settings.brightness);
}

static const uint8_t *saved_after_loading_version(uint8_t version)
{
    uint8_t bytes[SETTINGS_FILE_SIZE];
    Settings written = settings_defaults();
    written.brightness = BRIGHT;
    settings_encode(&written, bytes);
    bytes[4] = version;
    hash_store(bytes + 54, hash_update(HASH_START, bytes, 54));
    fake_hal_reset();
    CHECK(hal_internal_file_write(SETTINGS_STORE_PATH, bytes, sizeof bytes));
    settings_store_load(&settings);
    CHECK_EQUAL(settings_defaults().brightness, settings.settings.brightness);
    CHECK_EQUAL(version <= SETTINGS_FORMAT_VERSION, settings_store_save(&settings));
    uint32_t size = 0;
    const uint8_t *saved = fake_hal_internal_file_bytes(SETTINGS_STORE_PATH, &size);
    CHECK_EQUAL(SETTINGS_FILE_SIZE, size);
    return saved;
}

static void a_later_settings_version_gives_the_defaults_and_is_never_overwritten(void)
{
    const uint8_t *saved = saved_after_loading_version(16);
    CHECK_EQUAL(16, saved[4]);
    CHECK_EQUAL(BRIGHT, saved[6]);
}

static void a_settings_file_of_version_zero_is_replaced_by_the_defaults(void)
{
    CHECK_EQUAL(1, saved_after_loading_version(0)[4]);
}

static void a_shorter_newer_settings_file_is_never_overwritten(void)
{
    static const uint8_t bytes[] = {'D', 'S', 'S', 'T', 2, 0};
    fake_hal_reset();
    CHECK(hal_internal_file_write(SETTINGS_STORE_PATH, bytes, sizeof bytes));
    settings_store_load(&settings);
    CHECK(!settings_store_save(&settings));
    CHECK(settings.values_failed);
    uint32_t size = 0;
    const uint8_t *saved = fake_hal_internal_file_bytes(SETTINGS_STORE_PATH, &size);
    CHECK_EQUAL(sizeof bytes, size);
    CHECK_EQUAL(0, memcmp(bytes, saved, sizeof bytes));
}

static void an_unread_settings_store_never_overwrites_the_saved_files(void)
{
    fake_hal_reset();
    settings_store_load(&settings);
    settings.settings.brightness = BRIGHT;
    CHECK(settings_store_save(&settings));
    settings_store_load(&settings);
    settings.unread = true;
    settings.settings.brightness = settings_defaults().brightness;
    int writes = fake_hal_write_count();
    CHECK(!settings_store_save(&settings));
    CHECK(!settings_store_set_clock_offset(&settings, 60));
    CHECK(settings.values_failed);
    CHECK_EQUAL(writes, fake_hal_write_count());
    settings_store_load(&settings);
    CHECK(!settings.unread);
    CHECK_EQUAL(BRIGHT, settings.settings.brightness);
}

static void a_corrupted_settings_file_falls_back_to_the_defaults(void)
{
    static const uint8_t garbage[SETTINGS_FILE_SIZE] = {'D', 'S', 'S', 'T', 9};
    fake_hal_reset();
    CHECK(hal_internal_file_write(SETTINGS_STORE_PATH, garbage, sizeof garbage));
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

static void an_internal_song_goes_to_the_favorite_tracks_file_of_internal_storage(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    SpokenLibrary internal;
    Library *library = spoken_library_open_internal(&internal);
    char marked[300];
    snprintf(marked, sizeof marked, "\"/%s", SPOKEN_CHAPTER_ONE_PATH);
    uint16_t track = spoken_library_track(library, marked);
    CHECK_EQUAL(FAVORITE_PLAYLIST_ADDED, favorite_playlist_add(library, track));
    uint32_t size = 0;
    const char *inside = (const char *)fake_hal_file_bytes("/\""
                                                           "/Favorite Tracks.m3u",
                                                           &size);
    char expected[300];
    snprintf(expected, sizeof expected, "#EXTM3U\n%s\n", SPOKEN_CHAPTER_ONE_PATH);
    CHECK(inside != NULL && strncmp(inside, expected, strlen(expected)) == 0);
    CHECK(fake_hal_file_bytes(FAVORITE_PLAYLIST_PATH, &size) == NULL);
    CHECK_EQUAL(FAVORITE_PLAYLIST_PRESENT, favorite_playlist_add(library, track));
    spoken_library_close(&internal);
}

static uint32_t generation_after_adding(bool internal_volume, bool marked)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    fake_hal_set_internal_volume(internal_volume);
    CHECK(hal_internal_file_reserve(MUSIC_GENERATION_PATH, 512) == HAL_RESERVE_CREATED);
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    SpokenLibrary spoken;
    Library *library =
        marked ? spoken_library_open_internal(&spoken) : spoken_library_open(&spoken);
    char path[300];
    snprintf(path, sizeof path, "%s%s", marked ? "\"/" : "", SPOKEN_CHAPTER_ONE_PATH);
    CHECK_EQUAL(FAVORITE_PLAYLIST_ADDED,
                favorite_playlist_add(library, spoken_library_track(library, path)));
    spoken_library_close(&spoken);
    return music_check_generation();
}

static void a_favorite_written_to_internal_storage_moves_the_internal_music_generation(void)
{
    CHECK_EQUAL(1, generation_after_adding(false, true));
    CHECK_EQUAL(1, generation_after_adding(true, false));
    CHECK_EQUAL(0, generation_after_adding(false, false));
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

static void shaping_words_and_positions_survive_restart_and_plain_edits_clear_them(void)
{
    fake_hal_reset();
    CHECK_EQUAL(HAL_RESERVE_CREATED, hal_internal_file_reserve(PARAMETRIC_STORE_PATH, 4096));
    SettingsStore store = {.settings = settings_defaults()};
    ParametricStore file = {0};
    parametric_store_read(&file, NULL);
    SoundShaping shape = {.positions = {2, 1}, .active = true};
    ParametricEq base = {0};
    ParametricBank bank = {.used = 1};
    CHECK(sound_shaping_build(&bank.custom, &base, &shape));
    bank.shaping[0] = shape;
    bank.slots[0] = bank.custom;
    bank.shaping[1] = shape;
    CHECK(parametric_store_save(&file, &bank));
    store.settings.equalizer = EQUALIZER_PARAMETRIC;
    store.settings.parametric = bank.custom;
    CHECK(settings_store_save(&store));
    settings_store_load(&store);
    CHECK(strcmp(store.parametric.description, "Warm · Bright") == 0);
    CHECK_EQUAL(2, parametric_store_bank()->shaping[0].positions[0]);
    SettingsScreens screens;
    Player row_player = {0};
    Toast row_toast = {0};
    PowerPolicy power = {0};
    SettingsContext context = {
        .store = &store, .player = &row_player, .toast = &row_toast, .power = &power};
    settings_screens_init(&screens, context, &BOARD_THEME);
    const MenuItem *row = &screens.items[SETTINGS_CUSTOM_EQ_ITEM - 1];
    CHECK(strcmp(row->value(row->context), "Warm · Bright") == 0);

    EqualizerUi ui;
    Player player = {0};
    Toast toast = {0};
    equalizer_ui_init(&ui, &store, &player, &toast, &BOARD_THEME);
    ui.parametric.screen.enter(&ui.parametric, 0);
    CHECK(!ui.parametric.shaping_mode);
    ParametricEq eq = store.settings.parametric;
    eq.filters[0].gain++;
    CHECK(ui.parametric.config.commit(&ui, &eq, -1));
    CHECK(!parametric_store_bank()->shaping[0].active);
    CHECK_EQUAL(0, store.parametric.description[0]);
    CHECK(parametric_store_bank()->shaping[1].active);
}

static void save_graphic_slots(int first, int second)
{
    EqualizerSlots slots = {0};
    int8_t gains[SETTINGS_EQUALIZER_BANDS] = {4, -2};
    equalizer_slots_put(&slots, first, gains);
    equalizer_slots_put(&slots, second, gains);
    uint8_t bytes[EQUALIZER_SLOTS_FILE_SIZE];
    equalizer_slots_encode(&slots, bytes);
    CHECK(hal_internal_file_write(EQUALIZER_SLOTS_PATH, bytes, sizeof bytes));
}

static bool graphic_slot_kept(int slot)
{
    uint8_t bytes[EQUALIZER_SLOTS_FILE_SIZE];
    EqualizerSlots slots = {0};
    return hal_internal_file_read(EQUALIZER_SLOTS_PATH, bytes, sizeof bytes) &&
           equalizer_slots_decode(bytes, sizeof bytes, &slots) &&
           equalizer_slots_used(&slots, slot);
}

static void an_edit_after_the_equalizer_module_reloads_keeps_every_other_saved_slot(void)
{
    fake_hal_reset();
    CHECK_EQUAL(HAL_RESERVE_CREATED, hal_internal_file_reserve(PARAMETRIC_STORE_PATH, 4096));
    save_graphic_slots(0, 2);
    ParametricStore file = {0};
    parametric_store_read(&file, NULL);
    ParametricEq eq = {
        .count = 1, .preamp = -62, .filters = {{105, 70, -23, PARAMETRIC_PEAK, true}}};
    ParametricBank bank = {.custom = eq, .used = 1u << 1 | 1u << 3};
    bank.slots[1] = eq;
    bank.slots[3] = eq;
    CHECK(parametric_store_save(&file, &bank));
    SettingsStore store = {.settings = settings_defaults()};
    CHECK(settings_store_save(&store));
    settings_store_load(&store);
    EqualizerUi ui;
    Player player = {0};
    Toast toast = {0};
    equalizer_ui_init(&ui, &store, &player, &toast, &BOARD_THEME);
    *parametric_store_bank() = (ParametricBank){0};
    *ui.presets = (EqualizerPresets){0};
    equalizer_ui_reload(&ui);
    ParametricEq edited = eq;
    edited.filters[0].gain++;
    CHECK(ui.parametric.config.commit(&ui, &edited, 5));
    ParametricStore reread = {0};
    parametric_store_read(&reread, NULL);
    const ParametricBank *saved = parametric_store_bank();
    CHECK_EQUAL(1u << 1 | 1u << 3 | 1u << 5, saved->used);
    CHECK(parametric_same(&eq, &saved->slots[1]));
    CHECK(parametric_same(&eq, &saved->slots[3]));
    CHECK(parametric_same(&edited, &saved->slots[5]));
    CHECK(graphic_slot_kept(0));
    CHECK(graphic_slot_kept(2));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(bluetooth_records_bound_counts_and_terminate_names),
        TEST_CASE(malformed_bluetooth_records_restore_nothing),
        TEST_CASE(music_check_records_survive_versions_and_scan_interruptions),
        TEST_CASE(a_record_matches_only_when_both_drives_match),
        TEST_CASE(an_unknown_internal_half_is_accepted_only_without_internal_songs),
        TEST_CASE(an_old_record_reads_as_missing_and_the_generation_moves_with_bumps),
        TEST_CASE(shaping_words_and_positions_survive_restart_and_plain_edits_clear_them),
        TEST_CASE(matching_graphic_and_parametric_slots_survive_restart),
        TEST_CASE(an_edit_after_the_equalizer_module_reloads_keeps_every_other_saved_slot),
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
        TEST_CASE(a_settings_file_on_the_music_volume_is_ignored),
        TEST_CASE(a_later_settings_version_gives_the_defaults_and_is_never_overwritten),
        TEST_CASE(a_settings_file_of_version_zero_is_replaced_by_the_defaults),
        TEST_CASE(a_shorter_newer_settings_file_is_never_overwritten),
        TEST_CASE(a_corrupted_settings_file_falls_back_to_the_defaults),
        TEST_CASE(an_unread_settings_store_never_overwrites_the_saved_files),
        TEST_CASE(hardware_settings_reach_the_hal),
        TEST_CASE(toggling_an_album_adds_it_and_saves),
        TEST_CASE(favorites_are_loaded_again),
        TEST_CASE(adding_a_song_creates_the_playlist_with_a_header),
        TEST_CASE(an_internal_song_goes_to_the_favorite_tracks_file_of_internal_storage),
        TEST_CASE(a_favorite_written_to_internal_storage_moves_the_internal_music_generation),
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
