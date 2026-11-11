#include <string.h>

#include "app/app.h"
#include "app/debug_loop.h"
#include "app_internal.h"
#include "board_layout.h"
#include "card_files.h"
#include "check.h"
#include "core/frame_pacer.h"
#include "core/key_script.h"
#include "core/key_tracker.h"
#include "default_theme.h"
#include "discovery_screen.h"
#include "equalizer_presets.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "frame_overlay.h"
#include "gfx/canvas.h"
#include "gfx/text.h"
#include "hal/display.h"
#include "hal/system.h"
#include "home_art.h"
#include "library/cover_build.h"
#include "library/covers.h"
#include "library/playlist_reader.h"
#include "library_rescan.h"
#include "music_check.h"
#include "screen_modules.h"
#include "screens/charge_screen.h"
#include "settings/equalizer_slots.h"
#include "settings/settings.h"
#include "settings_store.h"
#include "ui/header.h"
#include "ui/slide.h"
#include "ui/status_bar.h"
#include "ui/toast.h"

enum {
    PLAYING_POSITION_MS = 60000,
    START_MS = 1000,
    KEY_HOLD_MS = 30,
    FULL_FRAME_PIXELS = HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT,
    SETTINGS_ABOUT_INDEX = 0,
    SETTINGS_SHUFFLE_INDEX = 1,
    DIAGNOSTICS_FRAME_TIME_INDEX = 0,
    DIAGNOSTICS_FIRMWARE_INDEX = 1,
    MUSIC_ARTISTS_INDEX = 2,
    MUSIC_ALBUMS_INDEX = 4,
    MUSIC_FOLDERS_INDEX = 6,
    BJORK_POSITION = 3,
    HOME_SETTINGS_INDEX = 1,
    HOME_NOW_PLAYING_INDEX = 2,
    SETTINGS_REPEAT_INDEX = 2,
    SETTINGS_SLEEP_INDEX = 4,
    SETTINGS_TIMEOUT_INDEX = 6,
    SETTINGS_BRIGHTNESS_INDEX = 7,
    SETTINGS_COLOR_INDEX = 8,
    SETTINGS_TEXT_SIZE_INDEX = 9,
    SETTINGS_ORIENTATION_INDEX = 10,
    SETTINGS_SKIP_INDEX = 11,
    SETTINGS_GAIN_INDEX = 12,
    SETTINGS_EQUALIZER_INDEX = 14,
    SETTINGS_CUSTOM_EQ_INDEX = 15,
    SETTINGS_START_INDEX = 18,
    SETTINGS_RESCAN_INDEX = 19,
    SETTINGS_THEME_INDEX = 21,
    MUSIC_COVER_FLOW_INDEX = 1,
    MUSIC_PLAYLISTS_INDEX = 8,
    SETTLE_STEPS = 30,
    RED_COVER = 0xF800,
    BLUE_COVER = 0x001F,
    MUSIC_FAVORITES_INDEX = 9,
    DEFAULT_TIMEOUT_MS = 30000,
    SHORT_TIMEOUT_MS = 10000,
    IDLE_MS = 20000,
    DIAGNOSTICS_CPU_CLOCK_INDEX = 2,
    DIAGNOSTICS_WATCHDOG_TEST_INDEX = 9,
    DIAGNOSTICS_STALL_TEST_INDEX = 5,
    DIAGNOSTICS_POWER_TAPS_INDEX = 6,
    DIAGNOSTICS_PROFILER_INDEX = 13,
    DEBUG_FOLLOW_MS = 100,
    SCRIPT_STEP_MS = 8,
    CHARGE_DARK_MS = 6000,
    SCRIPT_KEY_MS = KEY_SCRIPT_TAP_MS + KEY_SCRIPT_GAP_MS + 4 * SCRIPT_STEP_MS,
    FIFTEEN_MINUTES_MS = 15 * 60 * 1000,
    GOODBYE_MS = 1000,
    SPLASH_FADE_MS = 600,
    SPLASH_MS = 1100,
    HALF_BATTERY = 50
};

static const char HELLS_BELLS_PATH[] = "/AC DC/Back In Black/1-01 - Hells Bells.flac";
static const char CUE_AUDIO_PATH[] = "/Cue Artist/Cue Album/Cue Album.flac";
static const char SHOOT_TO_THRILL_PATH[] = "/AC DC/Back In Black/1-02 - Shoot to Thrill.flac";
static const char SHOOTS_PATH[] = "/acloudyskye/This Won't Be The Last Time/01 - Shoots.flac";

static uint32_t now_ms;

static const Theme *expected_theme(void)
{
    static Theme theme;
    SettingsStore store;
    settings_store_load(&store);
    theme = BOARD_THEME;
    default_theme_set_appearance(&theme, store.settings.appearance);
    default_theme_set_accent(&theme, store.settings.accent);
    default_theme_set_text_size(&theme, store.settings.text_size);
    default_theme_set_orientation(&theme, store.settings.orientation);
    return &theme;
}

static void prefill_player_files(void)
{
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
    fake_hal_add_file(MUSIC_CHECK_PATH, NULL, 4096);
    music_check_remember((MusicFingerprint){.complete = true});
}

static void start_app(void)
{
    fake_hal_reset();
    prefill_player_files();
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
}

static void start_app_with_database(const uint8_t *bytes, uint32_t size)
{
    uint32_t covers_size = 0;
    const uint8_t *covers = fixture_covers_bytes(&covers_size);
    fake_hal_reset();
    fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/library.db", bytes, size);
    fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/covers.bin", covers, covers_size);
    fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/build.tmp", NULL, 16 * 1024 * 1024);
    prefill_player_files();
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
}

static void start_app_with_library(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    start_app_with_database(bytes, size);
}

static void start_app_with_cue_library(void)
{
    static FixtureLibrary cue;
    fixture_cue_library_open(&cue);
    start_app_with_database(cue.bytes, cue.size);
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

static void hold_for(Key key, uint32_t duration_ms)
{
    fake_hal_queue_transition(key, true, now_ms);
    now_ms += duration_ms;
    step();
    fake_hal_queue_transition(key, false, now_ms);
    now_ms += 10;
    step();
}

static void hold(Key key)
{
    hold_for(key, 1000);
}

static void hold_power(void)
{
    hold_for(KEY_POWER, 2000);
}

static void hold_back(void)
{
    hold(KEY_BACK);
}

static void wait_ms(uint32_t duration_ms)
{
    now_ms += duration_ms;
    step();
}

static void restart_app(void)
{
    now_ms += 10;
    fake_hal_set_milliseconds(now_ms);
    app_start();
}

static void move_down(int times)
{
    for (int count = 0; count < times; count++) {
        tap(KEY_VOLUME_UP);
    }
}

static void move_up(int times)
{
    for (int count = 0; count < times; count++) {
        tap(KEY_VOLUME_DOWN);
    }
}

static bool title_is(const char *expected)
{
    return strcmp(expected, app_screen_title()) == 0;
}

static bool overlay_visible(void)
{
    Rect area = frame_overlay_area();
    return fake_hal_pixel(area.x, area.y) == frame_overlay_background();
}

static void audio_gets_a_turn_after_every_strip(void)
{
    start_app();
    int before = fake_hal_audio_service_count();
    step();
    CHECK(fake_hal_audio_service_count() - before >= HAL_DISPLAY_HEIGHT / BOARD_STRIP_HEIGHT + 1);
}

static void bluetooth_gets_a_turn_after_every_strip(void)
{
    start_app();
    int before = fake_hal_bluetooth_service_count();
    step();
    CHECK(fake_hal_bluetooth_service_count() - before >=
          HAL_DISPLAY_HEIGHT / BOARD_STRIP_HEIGHT + 1);
}

static void a_step_writes_one_full_frame_and_presents_it(void)
{
    start_app();
    step();
    CHECK_EQUAL(FULL_FRAME_PIXELS, fake_hal_pixels_written());
    CHECK_EQUAL(1, fake_hal_present_count());
}

static void steps_closer_than_the_frame_interval_draw_once(void)
{
    start_app();
    step();
    int presents = fake_hal_present_count();
    step();
    CHECK_EQUAL(presents, fake_hal_present_count());
    wait_ms(FRAME_PACER_ACTIVE_MS);
    CHECK_EQUAL(presents + 1, fake_hal_present_count());
}

static int presents_once_the_screen_rests(void)
{
    for (int frame = 0; frame < 300; frame++) {
        wait_ms(FRAME_PACER_ACTIVE_MS);
    }
    int presents = fake_hal_present_count();
    while (fake_hal_present_count() == presents) {
        wait_ms(1);
    }
    return fake_hal_present_count();
}

static void a_screen_that_stopped_changing_rests_until_a_key_arrives(void)
{
    start_app();
    int presents = presents_once_the_screen_rests();
    wait_ms(FRAME_PACER_REST_MS - 1);
    CHECK_EQUAL(presents, fake_hal_present_count());
    wait_ms(1);
    CHECK_EQUAL(presents + 1, fake_hal_present_count());
    fake_hal_queue_transition(KEY_VOLUME_UP, true, now_ms);
    wait_ms(FRAME_PACER_ACTIVE_MS);
    CHECK_EQUAL(presents + 2, fake_hal_present_count());
}

static void a_resting_screen_asks_for_slow_passes_and_input_brings_fast_ones_back(void)
{
    start_app();
    presents_once_the_screen_rests();
    CHECK_EQUAL(4, (int)app_pass_interval_ms());
    fake_hal_queue_transition(KEY_VOLUME_UP, true, now_ms);
    wait_ms(1);
    CHECK_EQUAL(1, (int)app_pass_interval_ms());
}

static void a_resting_screen_is_redrawn_soon_after_what_it_shows_changes(void)
{
    start_app();
    int presents = presents_once_the_screen_rests();
    fake_hal_set_battery(
        (HalBattery){.known = true, .percent = 41, .usb_power = true, .charging = true});
    wait_ms(FRAME_PACER_IDLE_MS);
    CHECK_EQUAL(presents + 1, fake_hal_present_count());
}

static void the_home_menu_is_the_root(void)
{
    start_app();
    step();
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(1, app_screen_depth());
}

static void selecting_music_opens_the_music_menu(void)
{
    start_app();
    tap(KEY_PLAY);
    CHECK(title_is("Music"));
    CHECK_EQUAL(2, app_screen_depth());
}

static void back_returns_to_the_previous_screen(void)
{
    start_app();
    tap(KEY_PLAY);
    tap(KEY_BACK);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
}

static void repeated_play_on_a_listening_leaf_keeps_one_back_to_its_menu(void)
{
    start_app();
    move_down(3);
    tap(KEY_PLAY);
    CHECK(title_is("Listening"));
    int depth = app_screen_depth();
    for (int row = 0; row < 5; row++) {
        move_down(row);
        tap(KEY_PLAY);
        for (int press = 0; press < 4; press++) {
            tap(KEY_PLAY);
            wait_ms(200);
        }
        CHECK_EQUAL(depth, app_screen_depth());
        tap(KEY_BACK);
        CHECK(title_is("Listening"));
        CHECK_EQUAL(depth, app_screen_depth());
        move_up(row);
    }
    tap(KEY_BACK);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
}

static void a_long_back_returns_home_from_deep_screens(void)
{
    start_app();
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK_EQUAL(3, app_screen_depth());
    hold_back();
    CHECK_EQUAL(1, app_screen_depth());
}

static void the_frame_overlay_is_off_until_enabled_in_diagnostics(void)
{
    start_app();
    step();
    CHECK(!overlay_visible());
    move_down(HOME_SETTINGS_INDEX);
    tap(KEY_PLAY);
    move_down(SETTINGS_ABOUT_INDEX);
    tap(KEY_PLAY);
    CHECK(title_is("About"));
    move_down(DIAGNOSTICS_FRAME_TIME_INDEX);
    tap(KEY_PLAY);
    CHECK(overlay_visible());
}

static void the_frame_overlay_splits_the_frame_time(void)
{
    char text[FRAME_OVERLAY_TEXT_CAPACITY];
    FrameBreakdown breakdown = {
        .total_us = 190400, .update_us = 12000, .draw_us = 119600, .send_us = 58200};
    frame_overlay_format(breakdown, text, sizeof text);
    CHECK(strcmp("190 u12 d120 s58", text) == 0);
}

static void the_frame_overlay_splits_the_drawing(void)
{
    char text[FRAME_OVERLAY_TEXT_CAPACITY];
    uint32_t parts[DRAW_PART_COUNT] = {[DRAW_PART_COVERS] = 9400, [DRAW_PART_REFLECTIONS] = 3100,
                                       [DRAW_PART_TEXT] = 2000,   [DRAW_PART_BACKDROP] = 600,
                                       [DRAW_PART_HASH] = 1500,   [DRAW_PART_AUDIO] = 0};
    frame_overlay_format_parts(parts, text, sizeof text);
    CHECK(strcmp("c9 r3 t2 b1 h2 a0", text) == 0);
}

static void the_frame_overlay_shows_the_audio_numbers(void)
{
    char text[FRAME_OVERLAY_TEXT_CAPACITY];
    HalAudioStats stats = {.buffered_ms = 340,
                           .underruns = 2,
                           .output_underruns = 7,
                           .decode_total_us = 100000,
                           .busy_us = 640000,
                           .elapsed_us = 1000000};
    frame_overlay_format_audio(stats, text, sizeof text);
    CHECK(strcmp("b340 x2 o7 L64%", text) == 0);
}

static void the_frame_overlay_shows_average_and_longest_block_times(void)
{
    char text[FRAME_OVERLAY_TEXT_CAPACITY];
    HalAudioStats stats = {.blocks = 4,
                           .decode_total_us = 92000,
                           .decode_max_us = 38400,
                           .read_total_us = 20000,
                           .read_max_us = 11600};
    frame_overlay_format_block_times(stats, text, sizeof text);
    CHECK(strcmp("d23/38 c5/12", text) == 0);
}

static void the_frame_overlay_shows_zero_before_any_block(void)
{
    char text[FRAME_OVERLAY_TEXT_CAPACITY];
    frame_overlay_format_block_times((HalAudioStats){0}, text, sizeof text);
    CHECK(strcmp("d0/0 c0/0", text) == 0);
    frame_overlay_format_audio((HalAudioStats){0}, text, sizeof text);
    CHECK(strcmp("b0 x0 o0 L0%", text) == 0);
}

static void open_music_entry(int index)
{
    tap(KEY_PLAY);
    move_down(index);
    tap(KEY_PLAY);
}

static bool columns_have_ink(int row, int first, int last)
{
    int top = expected_theme()->metrics.header_height + row * expected_theme()->metrics.row_height;
    for (int y = top; y < top + expected_theme()->metrics.row_height; y++) {
        for (int x = first; x < last; x++) {
            if (fake_hal_pixel(x, y) != expected_theme()->colors.background) {
                return true;
            }
        }
    }
    return false;
}

static bool row_shows_a_chevron(int row)
{
    int right = HAL_DISPLAY_WIDTH - expected_theme()->metrics.padding;
    int chevron = text_width(expected_theme()->fonts.title, "\xE2\x80\xBA");
    int gap = expected_theme()->metrics.detail_gap;
    return columns_have_ink(row, right - chevron, right) &&
           !columns_have_ink(row, right - chevron - gap, right - chevron);
}

static void album_rows_show_a_chevron_and_track_rows_do_not(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_ALBUMS_INDEX);
    wait_ms(SLIDE_DURATION_MS);
    CHECK(row_shows_a_chevron(1));
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    CHECK(!row_shows_a_chevron(1));
}

static void albums_open_from_the_library(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_ALBUMS_INDEX);
    CHECK(title_is("Albums"));
    tap(KEY_PLAY);
    CHECK(title_is("Back In Black"));
    CHECK_EQUAL(4, app_screen_depth());
}

static void a_cue_album_lists_its_cue_tracks_and_each_plays_its_own_part(void)
{
    start_app_with_cue_library();
    open_music_entry(MUSIC_ALBUMS_INDEX);
    move_down(1);
    tap(KEY_PLAY);
    CHECK(title_is("Cue Album"));
    tap(KEY_PLAY);
    CHECK_EQUAL(0, fake_hal_audio_play_start());
    CHECK_EQUAL(1000, fake_hal_audio_play_end());
    tap(KEY_BACK);
    move_down(2);
    tap(KEY_PLAY);
    CHECK_EQUAL(2493, fake_hal_audio_play_start());
    CHECK_EQUAL(0, fake_hal_audio_play_end());
    tap(KEY_BACK);
    move_down(1);
    tap(KEY_PLAY);
    CHECK_EQUAL(0, fake_hal_audio_play_start());
    CHECK_EQUAL(1000, fake_hal_audio_play_end());
}

static void open_cue_album_folder(void)
{
    start_app_with_cue_library();
    open_music_entry(MUSIC_FOLDERS_INDEX);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK(title_is("Cue Album"));
}

static void a_cue_backed_file_in_folders_plays_the_album_from_its_first_cue_track(void)
{
    open_cue_album_folder();
    move_down(1);
    tap(KEY_PLAY);
    CHECK_EQUAL(0, fake_hal_audio_play_start());
    CHECK_EQUAL(1000, fake_hal_audio_play_end());
    CHECK_EQUAL(1000, fake_hal_audio_queued_start());
    CHECK_EQUAL(2493, fake_hal_audio_queued_end());
}

static void a_cue_sheet_in_folders_opens_its_songs_and_plays_from_the_chosen_one(void)
{
    open_cue_album_folder();
    tap(KEY_PLAY);
    CHECK(title_is("Cue Album.cue"));
    move_down(1);
    tap(KEY_PLAY);
    CHECK_EQUAL(1000, fake_hal_audio_play_start());
    CHECK_EQUAL(2493, fake_hal_audio_play_end());
    CHECK_EQUAL(2493, fake_hal_audio_queued_start());
}

static void an_artist_leads_to_their_albums(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_ARTISTS_INDEX);
    move_down(BJORK_POSITION);
    tap(KEY_PLAY);
    CHECK(title_is("Björk"));
    tap(KEY_PLAY);
    CHECK(title_is("Homogenic"));
}

static void back_returns_to_the_previous_list(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_ALBUMS_INDEX);
    tap(KEY_PLAY);
    tap(KEY_BACK);
    CHECK(title_is("Albums"));
}

static void folders_can_be_browsed_down_to_an_album(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_FOLDERS_INDEX);
    CHECK(title_is("Folders"));
    tap(KEY_PLAY);
    CHECK(title_is("AC DC"));
    tap(KEY_PLAY);
    CHECK(title_is("Back In Black"));
    CHECK_EQUAL(5, app_screen_depth());
}

static void without_a_library_the_lists_explain_what_is_missing(void)
{
    start_app();
    open_music_entry(MUSIC_ARTISTS_INDEX);
    CHECK(title_is("No library"));
}

static bool playing_path_is(const char *expected)
{
    return strcmp(expected, fake_hal_audio_playing_path()) == 0;
}

static void play_first_album_track(void)
{
    open_music_entry(MUSIC_ALBUMS_INDEX);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
}

static void holding_menu_on_a_browse_list_opens_its_view_options(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_ALBUMS_INDEX);
    hold(KEY_MENU);
    CHECK(title_is("View options"));
    CHECK_EQUAL(SCREEN_MODULE_LIBRARY, (int)fake_hal_loaded_module());
    tap(KEY_BACK);
    CHECK(!title_is("Now Playing"));
}

static void holding_an_artist_publishes_only_that_artists_songs_before_playing(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_ARTISTS_INDEX);
    hold(KEY_PLAY);
    for (int pass = 0; pass < 3000 && !title_is("Now Playing"); pass++) {
        wait_ms(4);
    }
    CHECK(title_is("Now Playing"));
    static FixtureLibrary reference;
    fixture_library_open(&reference);
    BrowseItem chosen;
    CHECK(browse_item(&reference.library, browse_root_query(BROWSE_ARTISTS), 0, &chosen));
    PlayerStatus status = app_player_status();
    LibraryTrack current;
    CHECK(library_read_track(&reference.library, status.track, &current));
    CHECK_EQUAL(chosen.index, current.artist);
    uint32_t expected = 0;
    for (uint32_t id = 0; id < library_count(&reference.library, LIBRARY_TRACKS); id++) {
        LibraryTrack candidate;
        CHECK(library_read_track(&reference.library, id, &candidate));
        expected += candidate.artist == chosen.index ? 1u : 0u;
    }
    CHECK_EQUAL(expected, status.place.total);
    CHECK_EQUAL(PLAYER_PLAYING, status.state);
}

static void repeated_lyrics_and_menu_shortcuts_reuse_now_playing_depth(void)
{
    start_app_with_library();
    play_first_album_track();
    int depth = app_screen_depth();
    int plays = fake_hal_audio_play_count();
    uint16_t track = app_player_status().track;
    for (int turn = 0; turn < 10; turn++) {
        hold(KEY_MENU);
        CHECK(title_is("Lyrics"));
        CHECK_EQUAL(depth + 1, app_screen_depth());
        tap(KEY_MENU);
        CHECK(title_is("Now Playing"));
        CHECK_EQUAL(depth, app_screen_depth());
    }
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
    CHECK_EQUAL(track, app_player_status().track);
}

static void queue_actions_and_menu_shortcuts_reuse_now_playing_before_home(void)
{
    start_app_with_library();
    play_first_album_track();
    int depth = app_screen_depth();
    int plays = fake_hal_audio_play_count();
    for (int turn = 0; turn < 8; turn++) {
        tap(KEY_MENU);
        CHECK(title_is("Up Next"));
        hold(KEY_PLAY);
        CHECK(title_is("Edit Up Next"));
        tap(KEY_MENU);
        CHECK(title_is("Now Playing"));
        CHECK_EQUAL(depth, app_screen_depth());
    }
    tap(KEY_MENU);
    hold(KEY_PLAY);
    hold_back();
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(1, app_screen_depth());
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
}

static void holding_back_from_mixtape_name_reaches_home(void)
{
    start_app_with_library();
    move_down(2);
    tap(KEY_PLAY);
    CHECK(title_is("Mixtapes"));
    move_down(1);
    tap(KEY_PLAY);
    CHECK(title_is("Mixtape name"));
    hold_back();
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(1, app_screen_depth());
}

static void holding_back_from_a_busy_mixtape_save_reaches_home(void)
{
    start_app_with_library();
    play_first_album_track();
    hold_back();
    move_down(4);
    tap(KEY_PLAY);
    CHECK(title_is("Mixtapes"));
    tap(KEY_PLAY);
    CHECK(title_is("Mixtape name"));
    hold(KEY_PLAY);
    hold_back();
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(1, app_screen_depth());
}

static void a_song_actions_menu_opens_its_metadata_and_personal_tags(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_ALBUMS_INDEX);
    tap(KEY_PLAY);
    hold(KEY_PLAY);
    move_down(3);
    tap(KEY_PLAY);
    CHECK(title_is("Track info"));
    tap(KEY_BACK);
    tap(KEY_BACK);
    hold(KEY_PLAY);
    move_down(4);
    tap(KEY_PLAY);
    CHECK(title_is("My tags"));
}

static void selecting_a_track_plays_it_and_shows_now_playing(void)
{
    start_app_with_library();
    play_first_album_track();
    CHECK(title_is("Now Playing"));
    CHECK(playing_path_is(HELLS_BELLS_PATH));
}

static void now_playing_keys_control_the_player(void)
{
    start_app_with_library();
    play_first_album_track();
    uint8_t volume = fake_hal_audio_volume();
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(volume + 1, fake_hal_audio_volume());
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    hold(KEY_VOLUME_UP);
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(volume + 1, fake_hal_audio_volume());
    hold(KEY_VOLUME_DOWN);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
}

static void lists_still_move_as_soon_as_a_volume_key_goes_down(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_BACK);
    tap(KEY_BACK);
    CHECK(title_is("Albums"));
    fake_hal_queue_transition(KEY_VOLUME_UP, true, now_ms);
    now_ms += 10;
    step();
    tap(KEY_PLAY);
    CHECK(!title_is("Back In Black"));
}

static void the_horizon_stays_inside_its_panel(void)
{
    static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
    for (int index = 0; index < HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT; index++) {
        pixels[index] = 0xFFFF;
    }
    Theme theme = BOARD_THEME;
    theme.horizon = true;
    Rect panel = {300, 30, 180, 192};
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT});
    home_art_draw_backdrop(&canvas, panel, &theme);
    bool outside_unchanged = true;
    for (int y = 0; y < HAL_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < panel.x; x++) {
            outside_unchanged &= pixels[y * HAL_DISPLAY_WIDTH + x] == 0xFFFF;
        }
    }
    CHECK(outside_unchanged);
}

static uint16_t home_art_probe(void)
{
    return fake_hal_pixel(HAL_DISPLAY_WIDTH - 72, 88);
}

static void home_art_shows_a_library_cover_when_idle(void)
{
    start_app_with_library();
    step();
    step();
    CHECK_EQUAL(RED_COVER, home_art_probe());
}

static void home_art_moves_on_and_skips_albums_without_a_cover(void)
{
    start_app_with_library();
    step();
    wait_ms(HOME_ART_CYCLE_MS);
    step();
    CHECK_EQUAL(BLUE_COVER, home_art_probe());
}

static void home_art_keeps_the_playing_album(void)
{
    start_app_with_library();
    play_first_album_track();
    hold_back();
    wait_ms(SLIDE_DURATION_MS);
    for (int cycle = 0; cycle < 3; cycle++) {
        wait_ms(HOME_ART_CYCLE_MS);
    }
    step();
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(RED_COVER, home_art_probe());
}

static void the_home_menu_shows_now_playing_last_once_a_track_is_loaded(void)
{
    start_app_with_library();
    play_first_album_track();
    hold_back();
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    move_down(HOME_NOW_PLAYING_INDEX);
    tap(KEY_PLAY);
    CHECK(title_is("Now Playing"));
}

static void the_home_menu_hides_now_playing_with_nothing_loaded(void)
{
    start_app_with_library();
    move_down(5);
    tap(KEY_PLAY);
    CHECK(title_is("Music"));
}

static void open_setting(int index)
{
    move_down(HOME_SETTINGS_INDEX);
    tap(KEY_PLAY);
    move_down(index);
    tap(KEY_PLAY);
}

static void settings_choose_shuffle_and_repeat(void)
{
    start_app_with_library();
    open_setting(SETTINGS_SHUFFLE_INDEX);
    CHECK(title_is("Shuffle"));
    move_down(1);
    tap(KEY_PLAY);
    CHECK(title_is("Settings"));
    CHECK_EQUAL(SHUFFLE_SONGS, app_player_status().shuffle);
    move_down(SETTINGS_REPEAT_INDEX - SETTINGS_SHUFFLE_INDEX);
    tap(KEY_PLAY);
    CHECK(title_is("Repeat"));
    move_down(2);
    tap(KEY_PLAY);
    CHECK_EQUAL(REPEAT_ALL, app_player_status().repeat);
}

static bool saved_settings(Settings *settings)
{
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_internal_file_bytes(SETTINGS_STORE_PATH, &size);
    return bytes != NULL && settings_decode(bytes, size, settings);
}

static void brightness_is_applied_and_saved(void)
{
    start_app_with_library();
    CHECK_EQUAL(settings_defaults().brightness, fake_hal_display_brightness());
    open_setting(SETTINGS_BRIGHTNESS_INDEX);
    CHECK(title_is("Brightness"));
    move_down(1);
    tap(KEY_PLAY);
    CHECK_EQUAL(settings_defaults().brightness + 1, fake_hal_display_brightness());
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(settings_defaults().brightness + 1, settings.brightness);
}

static void gain_is_applied(void)
{
    start_app_with_library();
    open_setting(SETTINGS_GAIN_INDEX);
    move_down(1);
    tap(KEY_PLAY);
    CHECK_EQUAL(HAL_AUDIO_GAIN_HIGH, fake_hal_audio_gain());
}

static void an_equalizer_preset_reaches_the_audio_and_survives_a_restart(void)
{
    start_app_with_library();
    CHECK_EQUAL(0, fake_hal_audio_equalizer().band_count);
    open_setting(SETTINGS_EQUALIZER_INDEX);
    CHECK(title_is("Equalizer"));
    move_down(EQUALIZER_POP);
    tap(KEY_PLAY);
    CHECK(title_is("Settings"));
    HalAudioEqualizer pop = fake_hal_audio_equalizer();
    CHECK_EQUAL(3, pop.band_count);
    CHECK_EQUAL(100, pop.band_hz[0]);
    CHECK_EQUAL(8, pop.gain_half_db[0]);
    CHECK_EQUAL(20480, pop.pre_gain);
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(EQUALIZER_POP, settings.equalizer);
    hal_audio_set_equalizer(&(HalAudioEqualizer){.pre_gain = HAL_AUDIO_UNITY_PRE_GAIN});
    restart_app();
    CHECK_EQUAL(3, fake_hal_audio_equalizer().band_count);
}

static void browsing_equalizer_presets_previews_audio_but_saves_only_on_play(void)
{
    start_app_with_library();
    open_setting(SETTINGS_EQUALIZER_INDEX);
    move_down(EQUALIZER_POP);
    CHECK_EQUAL(3, fake_hal_audio_equalizer().band_count);
    CHECK_EQUAL(8, fake_hal_audio_equalizer().gain_half_db[0]);
    SettingsStore store;
    settings_store_load(&store);
    CHECK_EQUAL(EQUALIZER_OFF, store.settings.equalizer);
    tap(KEY_BACK);
    CHECK(title_is("Settings"));
    CHECK_EQUAL(0, fake_hal_audio_equalizer().band_count);
    tap(KEY_PLAY);
    move_down(EQUALIZER_POP);
    tap(KEY_PLAY);
    CHECK_EQUAL(3, fake_hal_audio_equalizer().band_count);
    settings_store_load(&store);
    CHECK_EQUAL(EQUALIZER_POP, store.settings.equalizer);
}

static void stopping_during_an_equalizer_preview_keeps_the_committed_curve(void)
{
    start_app_with_library();
    open_setting(SETTINGS_EQUALIZER_INDEX);
    move_down(EQUALIZER_POP);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(14336, fake_hal_audio_equalizer().pre_gain);
    Settings before;
    CHECK(saved_settings(&before));
    CHECK_EQUAL(EQUALIZER_POP, before.equalizer);
    app_stop();
    CHECK_EQUAL(20480, fake_hal_audio_equalizer().pre_gain);
    restart_app();
    CHECK_EQUAL(3, fake_hal_audio_equalizer().band_count);
    CHECK_EQUAL(8, fake_hal_audio_equalizer().gain_half_db[0]);
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(EQUALIZER_POP, settings.equalizer);
}

static void leaving_an_equalizer_preview_restores_the_curve_without_interrupting_music(void)
{
    start_app_with_library();
    play_first_album_track();
    fake_hal_audio_set_position(5432);
    PlayerStatus before = app_player_status();
    uint8_t volume = fake_hal_audio_volume();
    int gain = fake_hal_audio_gain();
    int output = fake_hal_audio_output();
    int dac_volume = fake_hal_dac_volume();
    hold_back();
    open_setting(SETTINGS_EQUALIZER_INDEX);
    move_down(EQUALIZER_POP);
    CHECK_EQUAL(3, fake_hal_audio_equalizer().band_count);
    tap(KEY_MENU);
    CHECK(title_is("Now Playing"));
    CHECK_EQUAL(0, fake_hal_audio_equalizer().band_count);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK_EQUAL(before.track, app_player_status().track);
    CHECK_EQUAL(5432, app_player_status().position_ms);
    CHECK_EQUAL(volume, fake_hal_audio_volume());
    CHECK_EQUAL(gain, fake_hal_audio_gain());
    CHECK_EQUAL(output, fake_hal_audio_output());
    CHECK_EQUAL(dac_volume, fake_hal_dac_volume());
    tap(KEY_MENU);
    CHECK(title_is("Up Next"));
    CHECK_EQUAL(0, fake_hal_audio_equalizer().band_count);
    tap(KEY_BACK);
    tap(KEY_BACK);
    CHECK(title_is("Equalizer"));
    move_down(EQUALIZER_POP);
    CHECK_EQUAL(3, fake_hal_audio_equalizer().band_count);
    tap(KEY_MENU);
    CHECK_EQUAL(0, fake_hal_audio_equalizer().band_count);
}

static void home_and_shutdown_discard_uncommitted_equalizer_previews(void)
{
    start_app_with_library();
    open_setting(SETTINGS_EQUALIZER_INDEX);
    move_down(EQUALIZER_POP);
    CHECK_EQUAL(3, fake_hal_audio_equalizer().band_count);
    hold_back();
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(0, fake_hal_audio_equalizer().band_count);
    restart_app();
    wait_ms(SPLASH_MS);
    open_setting(SETTINGS_EQUALIZER_INDEX);
    move_down(EQUALIZER_POP);
    hold_power();
    CHECK_EQUAL(0, fake_hal_audio_equalizer().band_count);
    SettingsStore store;
    settings_store_load(&store);
    CHECK_EQUAL(EQUALIZER_OFF, store.settings.equalizer);
}

static void custom_eq_adjusts_a_band_with_the_volume_keys(void)
{
    start_app_with_library();
    open_setting(SETTINGS_CUSTOM_EQ_INDEX);
    CHECK(title_is("Custom EQ"));
    move_down(5);
    tap(KEY_PLAY);
    tap(KEY_VOLUME_UP);
    tap(KEY_VOLUME_UP);
    tap(KEY_VOLUME_UP);
    tap(KEY_VOLUME_DOWN);
    HalAudioEqualizer custom = fake_hal_audio_equalizer();
    CHECK_EQUAL(1, custom.band_count);
    CHECK_EQUAL(1000, custom.band_hz[0]);
    CHECK_EQUAL(2, custom.gain_half_db[0]);
    tap(KEY_PLAY);
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(EQUALIZER_CUSTOM, settings.equalizer);
    CHECK_EQUAL(2, settings.equalizer_gains[5]);
    CHECK_EQUAL(0, settings.equalizer_gains[1]);
    tap(KEY_BACK);
    CHECK(title_is("Settings"));
}

static void custom_eq_stops_at_ten_decibels(void)
{
    start_app_with_library();
    open_setting(SETTINGS_CUSTOM_EQ_INDEX);
    tap(KEY_PLAY);
    for (int press = 0; press < 24; press++) {
        tap(KEY_VOLUME_DOWN);
    }
    CHECK_EQUAL(-20, fake_hal_audio_equalizer().gain_half_db[0]);
    tap(KEY_PLAY);
    CHECK(title_is("Custom EQ"));
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(-20, settings.equalizer_gains[0]);
}

static void custom_eq_back_discards_the_audible_draft_without_saving_it(void)
{
    start_app_with_library();
    open_setting(SETTINGS_CUSTOM_EQ_INDEX);
    tap(KEY_PLAY);
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(1, fake_hal_audio_equalizer().band_count);
    CHECK_EQUAL(1, fake_hal_audio_equalizer().gain_half_db[0]);
    SettingsStore store;
    settings_store_load(&store);
    CHECK_EQUAL(EQUALIZER_OFF, store.settings.equalizer);
    CHECK_EQUAL(0, store.settings.equalizer_gains[0]);
    tap(KEY_BACK);
    CHECK(title_is("Custom EQ"));
    CHECK_EQUAL(0, fake_hal_audio_equalizer().band_count);
    tap(KEY_PLAY);
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(1, fake_hal_audio_equalizer().band_count);
    hold_back();
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(0, fake_hal_audio_equalizer().band_count);
    restart_app();
    settings_store_load(&store);
    CHECK_EQUAL(EQUALIZER_OFF, store.settings.equalizer);
    CHECK_EQUAL(0, store.settings.equalizer_gains[0]);
}

static void save_band_five_to_the_second_slot(void)
{
    open_setting(SETTINGS_CUSTOM_EQ_INDEX);
    move_down(5);
    tap(KEY_PLAY);
    for (int press = 0; press < 3; press++) {
        tap(KEY_VOLUME_UP);
    }
    tap(KEY_PLAY);
    move_down(SETTINGS_EQUALIZER_BANDS - 5);
    tap(KEY_PLAY);
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
}

static bool saved_slots(EqualizerSlots *slots)
{
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_internal_file_bytes(EQUALIZER_SLOTS_PATH, &size);
    return bytes != NULL && equalizer_slots_decode(bytes, size, slots);
}

static void custom_eq_saves_the_curve_into_a_numbered_slot(void)
{
    start_app_with_library();
    save_band_five_to_the_second_slot();
    CHECK(title_is("Custom EQ"));
    EqualizerSlots slots;
    CHECK(saved_slots(&slots));
    CHECK(!equalizer_slots_used(&slots, 0));
    CHECK(equalizer_slots_used(&slots, 1));
    CHECK_EQUAL(3, slots.gains[1][5]);
    CHECK_EQUAL(0, slots.gains[1][4]);
}

static void a_saved_slot_is_listed_after_the_presets_and_restores_its_curve(void)
{
    start_app_with_library();
    save_band_five_to_the_second_slot();
    move_up(SETTINGS_EQUALIZER_BANDS - 5);
    tap(KEY_PLAY);
    tap(KEY_VOLUME_UP);
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(5, settings.equalizer_gains[5]);
    tap(KEY_BACK);
    move_up(1);
    tap(KEY_PLAY);
    CHECK(title_is("Equalizer"));
    move_down(1);
    tap(KEY_PLAY);
    CHECK(title_is("Custom EQ"));
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(EQUALIZER_CUSTOM, settings.equalizer);
    CHECK_EQUAL(3, settings.equalizer_gains[5]);
    CHECK_EQUAL(3, fake_hal_audio_equalizer().gain_half_db[0]);
    CHECK_EQUAL(1000, fake_hal_audio_equalizer().band_hz[0]);
}

static void a_chosen_slot_opens_its_editor_and_saves_back_into_it(void)
{
    start_app_with_library();
    save_band_five_to_the_second_slot();
    tap(KEY_BACK);
    move_up(1);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK(title_is("Custom EQ"));
    move_down(5);
    tap(KEY_PLAY);
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    move_down(SETTINGS_EQUALIZER_BANDS - 5);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    EqualizerSlots slots;
    CHECK(saved_slots(&slots));
    CHECK(!equalizer_slots_used(&slots, 0));
    CHECK_EQUAL(4, slots.gains[1][5]);
    tap(KEY_BACK);
    CHECK(title_is("Settings"));
}

static void the_equalizer_row_names_the_matching_slot(void)
{
    EqualizerPresets presets = {0};
    int8_t gains[SETTINGS_EQUALIZER_BANDS] = {0};
    gains[5] = 3;
    equalizer_slots_put(&presets.slots, 2, gains);
    equalizer_slots_put(&presets.slots, 5, gains);
    CHECK_EQUAL(2, equalizer_presets_matching_slot(&presets, gains));
    CHECK_EQUAL(EQUALIZER_PRESET_COUNT + 1, equalizer_presets_slot_option(&presets, 5));
    gains[5] = 4;
    CHECK_EQUAL(-1, equalizer_presets_matching_slot(&presets, gains));
    CHECK(strcmp(equalizer_presets_slot_label(2), "Slot 3") == 0);
}

static void an_eq_file_is_listed_by_name_and_loaded_on_choosing(void)
{
    start_app_with_library();
    fake_hal_add_eq_file("warm.txt", "GraphicEQ: 20 2; 20000 2\n");
    open_setting(SETTINGS_EQUALIZER_INDEX);
    move_down(EQUALIZER_PRESET_COUNT);
    tap(KEY_PLAY);
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(EQUALIZER_CUSTOM, settings.equalizer);
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        CHECK_EQUAL(4, settings.equalizer_gains[band]);
    }
}

static void an_unreadable_eq_file_changes_nothing(void)
{
    start_app_with_library();
    fake_hal_add_eq_file("empty.txt", "no curve here\n");
    open_setting(SETTINGS_EQUALIZER_INDEX);
    move_down(EQUALIZER_PRESET_COUNT);
    tap(KEY_PLAY);
    Settings settings;
    CHECK(saved_settings(&settings) == false || settings.equalizer == EQUALIZER_OFF);
    CHECK_EQUAL(0, fake_hal_audio_equalizer().band_count);
}

static void saved_settings_are_restored_at_start(void)
{
    start_app_with_library();
    open_setting(SETTINGS_BRIGHTNESS_INDEX);
    move_down(2);
    tap(KEY_PLAY);
    restart_app();
    CHECK_EQUAL(settings_defaults().brightness + 2, fake_hal_display_brightness());
}

static bool selection_is_red(void)
{
    const ThemeMetrics *metrics = &expected_theme()->metrics;
    uint16_t pixel =
        fake_hal_pixel(metrics->padding / 2, metrics->header_height + metrics->row_height / 2);
    return (pixel >> 11) > (pixel & 0x1F);
}

static void the_color_setting_recolors_the_selection_and_is_saved(void)
{
    start_app_with_library();
    Settings chosen = settings_defaults();
    chosen.appearance = APPEARANCE_GRAPHITE;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&chosen, bytes);
    CHECK(hal_internal_file_write(SETTINGS_STORE_PATH, bytes, sizeof bytes));
    restart_app();
    wait_ms(1000);
    CHECK(!selection_is_red());
    open_setting(SETTINGS_COLOR_INDEX);
    CHECK(title_is("Color"));
    move_down(ACCENT_RED);
    tap(KEY_PLAY);
    CHECK(title_is("Settings"));
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(ACCENT_RED, settings.accent);
    restart_app();
    wait_ms(1000);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK(selection_is_red());
}

static void open_theme_with_a_saved_graphite_palette(void)
{
    start_app_with_library();
    SettingsStore store;
    settings_store_load(&store);
    store.settings.appearance = APPEARANCE_GRAPHITE;
    CHECK(settings_store_save(&store));
    restart_app();
    wait_ms(SPLASH_MS);
    open_setting(SETTINGS_THEME_INDEX);
    wait_ms(SLIDE_DURATION_MS);
    CHECK(title_is("Theme"));
}

static void check_theme_header(AppearanceTheme appearance)
{
    Theme theme = *expected_theme();
    default_theme_set_appearance(&theme, appearance);
    Color divider = theme.header_rule ? theme.colors.accent : theme.colors.divider;
    CHECK_EQUAL(divider, fake_hal_pixel(2, theme.metrics.header_height - 1));
}

static void scrolling_themes_previews_each_palette_without_saving(void)
{
    open_theme_with_a_saved_graphite_palette();
    for (int index = 0; index < APPEARANCE_THEME_COUNT; index++) {
        check_theme_header((AppearanceTheme)index);
        Settings settings;
        CHECK(saved_settings(&settings));
        CHECK_EQUAL(APPEARANCE_GRAPHITE, settings.appearance);
        tap(KEY_VOLUME_UP);
    }
    check_theme_header(APPEARANCE_GRAPHITE);
    tap(KEY_VOLUME_DOWN);
    check_theme_header(APPEARANCE_CLASSIC);
}

static void back_from_a_theme_preview_restores_the_saved_palette(void)
{
    open_theme_with_a_saved_graphite_palette();
    tap(KEY_VOLUME_UP);
    check_theme_header(APPEARANCE_NEON_NOIR);
    tap(KEY_BACK);
    CHECK(title_is("Settings"));
    check_theme_header(APPEARANCE_GRAPHITE);
    tap(KEY_PLAY);
    CHECK(title_is("Theme"));
    check_theme_header(APPEARANCE_GRAPHITE);
    tap(KEY_BACK);
    move_up(SETTINGS_THEME_INDEX - SETTINGS_CUSTOM_EQ_INDEX);
    tap(KEY_PLAY);
    CHECK(title_is("Custom EQ"));
    check_theme_header(APPEARANCE_GRAPHITE);
}

static void choosing_a_preview_saves_it_and_keeps_it_after_restart(void)
{
    open_theme_with_a_saved_graphite_palette();
    move_down(APPEARANCE_SUNSET);
    tap(KEY_PLAY);
    CHECK(title_is("Settings"));
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(APPEARANCE_SUNSET, settings.appearance);
    check_theme_header(APPEARANCE_SUNSET);
    restart_app();
    wait_ms(SPLASH_MS);
    check_theme_header(APPEARANCE_SUNSET);
}

static void leaving_or_stopping_during_a_preview_keeps_the_saved_palette(void)
{
    open_theme_with_a_saved_graphite_palette();
    tap(KEY_VOLUME_UP);
    check_theme_header(APPEARANCE_NEON_NOIR);
    hold_back();
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    check_theme_header(APPEARANCE_GRAPHITE);
    restart_app();
    wait_ms(SPLASH_MS);
    open_setting(SETTINGS_THEME_INDEX);
    tap(KEY_VOLUME_DOWN);
    check_theme_header(APPEARANCE_CLASSIC);
    app_stop();
    restart_app();
    wait_ms(SPLASH_MS);
    check_theme_header(APPEARANCE_GRAPHITE);
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(APPEARANCE_GRAPHITE, settings.appearance);
}

static bool selected_at(int y)
{
    return fake_hal_pixel(expected_theme()->metrics.padding / 2, y) !=
           expected_theme()->colors.background;
}

static void choose_text_size(TextSize size)
{
    open_setting(SETTINGS_TEXT_SIZE_INDEX);
    CHECK(title_is("Text size"));
    move_down((int)size - (int)TEXT_SIZE_MEDIUM);
    move_up((int)TEXT_SIZE_MEDIUM - (int)size);
    tap(KEY_PLAY);
    CHECK(title_is("Settings"));
    wait_ms(SLIDE_DURATION_MS);
    step();
}

static void large_text_resizes_the_open_list_at_once_and_is_saved(void)
{
    start_app_with_library();
    choose_text_size(TEXT_SIZE_LARGE);
    int header = expected_theme()->metrics.header_height;
    CHECK(selected_at(header + 4 * 38 + 2));
    CHECK(!selected_at(header + 4 * 38 - 2));
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(TEXT_SIZE_LARGE, settings.text_size);
}

static void small_text_resizes_the_open_list_at_once(void)
{
    start_app_with_library();
    choose_text_size(TEXT_SIZE_SMALL);
    int header = expected_theme()->metrics.header_height;
    CHECK(selected_at(header + 5 * 24 + 2));
    CHECK(!selected_at(header + 5 * 24 + 26));
    CHECK(!selected_at(header + 5 * 24 - 2));
}

static void the_text_size_survives_a_restart(void)
{
    start_app_with_library();
    choose_text_size(TEXT_SIZE_LARGE);
    restart_app();
    wait_ms(1000);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    int header = expected_theme()->metrics.header_height;
    CHECK(selected_at(header + 36));
    CHECK(!selected_at(header + 40));
}

static void the_home_menu_fits_its_rows_to_the_new_size_when_shown_again(void)
{
    start_app_with_library();
    choose_text_size(TEXT_SIZE_LARGE);
    tap(KEY_BACK);
    wait_ms(SLIDE_DURATION_MS);
    step();
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    int header = expected_theme()->metrics.header_height;
    CHECK(selected_at(header + 38 + 36));
    CHECK(!selected_at(header + 38 + 40));
}

static void settle(void);

static void choose_orientation(Orientation orientation)
{
    open_setting(SETTINGS_ORIENTATION_INDEX);
    CHECK(title_is("Orientation"));
    move_down((int)orientation);
    tap(KEY_PLAY);
    CHECK(title_is("Settings"));
    wait_ms(SLIDE_DURATION_MS);
    step();
}

static void portrait_down(int times)
{
    for (int count = 0; count < times; count++) {
        tap(KEY_VOLUME_DOWN);
    }
}

static bool drew_the_bottom_of_a_portrait_screen(void)
{
    return fake_hal_pixel(2, HAL_DISPLAY_LONG_SIDE - 1) != 0;
}

static void the_orientation_setting_is_saved(void)
{
    start_app_with_library();
    CHECK_EQUAL(HAL_DISPLAY_LANDSCAPE, fake_hal_display_rotation());
    choose_orientation(ORIENTATION_PORTRAIT_FLIPPED);
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(ORIENTATION_PORTRAIT_FLIPPED, settings.orientation);
}

static void choosing_portrait_turns_the_screen_and_redraws_it_upright(void)
{
    start_app_with_library();
    choose_orientation(ORIENTATION_PORTRAIT);
    CHECK_EQUAL(HAL_DISPLAY_PORTRAIT, fake_hal_display_rotation());
    CHECK(drew_the_bottom_of_a_portrait_screen());
}

static void the_flipped_portrait_turns_the_panel_the_other_way(void)
{
    start_app_with_library();
    choose_orientation(ORIENTATION_PORTRAIT_FLIPPED);
    CHECK_EQUAL(HAL_DISPLAY_PORTRAIT_FLIPPED, fake_hal_display_rotation());
    CHECK(drew_the_bottom_of_a_portrait_screen());
}

static void choosing_landscape_again_turns_the_screen_back(void)
{
    start_app_with_library();
    choose_orientation(ORIENTATION_PORTRAIT);
    tap(KEY_PLAY);
    CHECK(title_is("Orientation"));
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    step();
    CHECK_EQUAL(HAL_DISPLAY_LANDSCAPE, fake_hal_display_rotation());
}

static void the_screen_starts_in_the_saved_orientation(void)
{
    start_app_with_library();
    choose_orientation(ORIENTATION_PORTRAIT);
    restart_app();
    settle();
    CHECK_EQUAL(HAL_DISPLAY_PORTRAIT, fake_hal_display_rotation());
    CHECK(drew_the_bottom_of_a_portrait_screen());
}

static void portrait_home_and_lists_keep_their_title_below_the_status_row(void)
{
    start_app_with_library();
    choose_orientation(ORIENTATION_PORTRAIT);
    hold_back();
    wait_ms(SLIDE_DURATION_MS);
    step();
    Theme theme = *expected_theme();
    default_theme_set_orientation(&theme, ORIENTATION_PORTRAIT);
    Rect title = theme_title_area(&theme);
    CHECK(title.y > 0);
    CHECK(fake_hal_pixel(HAL_DISPLAY_SHORT_SIDE - 1, theme.metrics.header_height - 1) ==
          (theme.header_rule ? theme.colors.accent : theme.colors.divider));
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    step();
    CHECK(title_is("Music"));
    CHECK(fake_hal_pixel(HAL_DISPLAY_SHORT_SIDE - 1, theme.metrics.header_height - 1) ==
          (theme.header_rule ? theme.colors.accent : theme.colors.divider));
}

static void in_portrait_the_volume_down_key_moves_down_the_list(void)
{
    start_app_with_library();
    choose_orientation(ORIENTATION_PORTRAIT);
    portrait_down(1);
    tap(KEY_PLAY);
    CHECK(title_is("Skip songs"));
}

static void in_the_flipped_portrait_the_rocker_keeps_its_landscape_direction(void)
{
    start_app_with_library();
    choose_orientation(ORIENTATION_PORTRAIT_FLIPPED);
    tap(KEY_VOLUME_DOWN);
    tap(KEY_PLAY);
    CHECK(title_is("Text size"));
}

static void a_key_held_across_the_turn_keeps_the_direction_it_started_with(void)
{
    start_app_with_library();
    choose_orientation(ORIENTATION_PORTRAIT);
    fake_hal_queue_transition(KEY_VOLUME_DOWN, true, now_ms);
    now_ms += KEY_HOLD_MS;
    step();
    fake_hal_queue_transition(KEY_VOLUME_DOWN, false, now_ms);
    now_ms += 10;
    step();
    tap(KEY_PLAY);
    CHECK(title_is("Skip songs"));
}

static void home_puts_the_album_art_under_the_menu_in_portrait(void)
{
    start_app_with_library();
    choose_orientation(ORIENTATION_PORTRAIT);
    hold_back();
    settle();
    Theme theme = *expected_theme();
    default_theme_set_orientation(&theme, ORIENTATION_PORTRAIT);
    int list_bottom = theme.metrics.header_height + 5 * theme.metrics.row_height;
    int cover_middle = list_bottom + 8 + COVER_FULL_SIZE / 2;
    CHECK_EQUAL(RED_COVER, fake_hal_pixel(HAL_DISPLAY_SHORT_SIDE / 2, cover_middle));
    CHECK(fake_hal_pixel(2, list_bottom) == theme.colors.divider);
}

static void now_playing_centres_the_cover_under_the_status_row_in_portrait(void)
{
    start_app_with_library();
    choose_orientation(ORIENTATION_PORTRAIT);
    hold_back();
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    portrait_down(MUSIC_ALBUMS_INDEX);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    step();
    CHECK(title_is("Now Playing"));
    const NowPlayingLayout *layout = &BOARD_LAYOUT.now_playing_portrait[TEXT_SIZE_MEDIUM];
    CHECK_EQUAL(0xF800, fake_hal_pixel(HAL_DISPLAY_SHORT_SIDE / 2,
                                       layout->cover_top + COVER_FULL_SIZE / 2));
    CHECK(fake_hal_pixel(2, layout->cover_top + COVER_FULL_SIZE / 2) != RED_COVER);
}

static void cover_flow_stacks_the_albums_in_portrait(void)
{
    start_app_with_library();
    choose_orientation(ORIENTATION_PORTRAIT);
    hold_back();
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    portrait_down(MUSIC_COVER_FLOW_INDEX);
    tap(KEY_PLAY);
    settle();
    CHECK(title_is("Cover Flow"));
    const CoverStackLayout *stack = &BOARD_LAYOUT.cover_stack;
    int middle = stack->center_top + stack->center_size / 2;
    CHECK_EQUAL(BLUE_COVER, fake_hal_pixel(stack->center_x, middle));
    portrait_down(1);
    settle();
    CHECK_EQUAL(RED_COVER, fake_hal_pixel(stack->center_x, middle));
}

static void play_with_skip_gesture(SkipGesture gesture)
{
    start_app_with_library();
    open_setting(SETTINGS_SKIP_INDEX);
    CHECK(title_is("Skip songs"));
    move_down((int)gesture);
    tap(KEY_PLAY);
    CHECK(title_is("Settings"));
    restart_app();
    wait_ms(1000);
    play_first_album_track();
    CHECK(title_is("Now Playing"));
}

static void skip_songs_defaults_to_hold_and_is_saved(void)
{
    start_app_with_library();
    open_setting(SETTINGS_SKIP_INDEX);
    CHECK_EQUAL(SKIP_GESTURE_HOLD, settings_defaults().skip_gesture);
    move_down(SKIP_GESTURE_PLAY_CLICKS);
    tap(KEY_PLAY);
    Settings settings;
    CHECK(saved_settings(&settings));
    CHECK_EQUAL(SKIP_GESTURE_PLAY_CLICKS, settings.skip_gesture);
}

static void double_volume_clicks_skip_songs_and_single_clicks_change_the_volume(void)
{
    play_with_skip_gesture(SKIP_GESTURE_DOUBLE_VOLUME);
    tap(KEY_VOLUME_UP);
    tap(KEY_VOLUME_UP);
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
    tap(KEY_VOLUME_DOWN);
    tap(KEY_VOLUME_DOWN);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
    uint8_t volume = fake_hal_audio_volume();
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(volume, fake_hal_audio_volume());
    wait_ms(400);
    CHECK_EQUAL(volume + 1, fake_hal_audio_volume());
    tap(KEY_VOLUME_DOWN);
    wait_ms(400);
    CHECK_EQUAL(volume, fake_hal_audio_volume());
    CHECK(playing_path_is(HELLS_BELLS_PATH));
}

static void holding_volume_seeks_with_the_double_volume_setting(void)
{
    play_with_skip_gesture(SKIP_GESTURE_DOUBLE_VOLUME);
    uint8_t volume = fake_hal_audio_volume();
    int seeks = fake_hal_audio_seek_count();
    hold(KEY_VOLUME_UP);
    CHECK(fake_hal_audio_seek_count() > seeks);
    CHECK_EQUAL(volume, fake_hal_audio_volume());
    CHECK(playing_path_is(HELLS_BELLS_PATH));
}

static void holding_volume_down_rewinds_with_the_double_volume_setting(void)
{
    play_with_skip_gesture(SKIP_GESTURE_DOUBLE_VOLUME);
    fake_hal_audio_set_position(PLAYING_POSITION_MS);
    step();
    uint32_t before = app_player_status().position_ms;
    CHECK_EQUAL(PLAYING_POSITION_MS, before);
    hold_for(KEY_VOLUME_DOWN, 600);
    CHECK(app_player_status().position_ms + 4000 < before);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
}

static void holding_volume_up_fast_forwards_with_the_screen_off(void)
{
    play_with_skip_gesture(SKIP_GESTURE_DOUBLE_VOLUME);
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    uint32_t before = app_player_status().position_ms;
    uint8_t volume = fake_hal_audio_volume();
    hold_for(KEY_VOLUME_UP, 600);
    CHECK(app_player_status().position_ms > before + 4000);
    CHECK_EQUAL(volume, fake_hal_audio_volume());
    CHECK(playing_path_is(HELLS_BELLS_PATH));
    CHECK(!fake_hal_display_on());
}

static void holding_volume_down_rewinds_with_the_screen_off(void)
{
    play_with_skip_gesture(SKIP_GESTURE_DOUBLE_VOLUME);
    fake_hal_audio_set_position(PLAYING_POSITION_MS);
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    uint32_t before = app_player_status().position_ms;
    hold_for(KEY_VOLUME_DOWN, 600);
    CHECK(app_player_status().position_ms + 4000 < before);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
}

static void hold_stepping(Key key, uint32_t duration_ms)
{
    fake_hal_queue_transition(key, true, now_ms);
    for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += 50) {
        now_ms += 50;
        step();
    }
    fake_hal_queue_transition(key, false, now_ms);
    now_ms += 10;
    step();
}

static void holding_volume_after_a_volume_step_keeps_changing_it(SkipGesture gesture,
                                                                 bool screen_off)
{
    play_with_skip_gesture(gesture);
    if (screen_off) {
        tap(KEY_POWER);
        CHECK(!fake_hal_display_on());
    }
    uint8_t volume = fake_hal_audio_volume();
    int seeks = fake_hal_audio_seek_count();
    tap(KEY_VOLUME_UP);
    wait_ms(400);
    CHECK_EQUAL(volume + 1, fake_hal_audio_volume());
    hold_stepping(KEY_VOLUME_UP, 1500);
    CHECK(fake_hal_audio_volume() > volume + 3);
    CHECK_EQUAL(seeks, fake_hal_audio_seek_count());
    CHECK(playing_path_is(HELLS_BELLS_PATH));
    uint8_t raised = fake_hal_audio_volume();
    hold_stepping(KEY_VOLUME_DOWN, 1500);
    CHECK(fake_hal_audio_volume() + 3 < raised);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
    tap(KEY_VOLUME_UP);
    wait_ms(400);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    uint8_t before_skip = fake_hal_audio_volume();
    hold(KEY_VOLUME_UP);
    CHECK_EQUAL(before_skip, fake_hal_audio_volume());
}

static void holding_volume_after_a_step_changes_it_with_the_hold_setting(void)
{
    holding_volume_after_a_volume_step_keeps_changing_it(SKIP_GESTURE_HOLD, false);
}

static void holding_volume_after_a_step_changes_it_with_the_double_volume_setting(void)
{
    holding_volume_after_a_volume_step_keeps_changing_it(SKIP_GESTURE_DOUBLE_VOLUME, false);
}

static void holding_volume_after_a_step_changes_it_with_the_screen_off(void)
{
    holding_volume_after_a_volume_step_keeps_changing_it(SKIP_GESTURE_HOLD, true);
    holding_volume_after_a_volume_step_keeps_changing_it(SKIP_GESTURE_DOUBLE_VOLUME, true);
}

static void hold_play_and_power(void)
{
    fake_hal_queue_transition(KEY_PLAY, true, now_ms);
    fake_hal_queue_transition(KEY_POWER, true, now_ms + 40);
    for (int index = 0; index < 24; index++) {
        now_ms += 50;
        step();
    }
    fake_hal_queue_transition(KEY_PLAY, false, now_ms);
    fake_hal_queue_transition(KEY_POWER, false, now_ms + 20);
    now_ms += 30;
    step();
}

static void play_and_power_lock_and_unlock_the_buttons(void)
{
    start_app_with_library();
    play_first_album_track();
    CHECK(title_is("Now Playing"));
    hold_play_and_power();
    CHECK(title_is("Now Playing"));
    uint8_t volume = fake_hal_audio_volume();
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    tap(KEY_VOLUME_UP);
    hold(KEY_VOLUME_UP);
    CHECK_EQUAL(volume, fake_hal_audio_volume());
    CHECK(playing_path_is(HELLS_BELLS_PATH));
    tap(KEY_BACK);
    tap(KEY_MENU);
    CHECK(title_is("Now Playing"));
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    tap(KEY_POWER);
    CHECK(fake_hal_display_on());
    hold_play_and_power();
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
}

static void the_lock_works_with_the_screen_off(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    hold_play_and_power();
    CHECK(fake_hal_display_on());
    CHECK(app_toast_text() != NULL && strcmp(app_toast_text(), "Buttons locked") == 0);
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    hold_play_and_power();
    CHECK(app_toast_text() != NULL && strcmp(app_toast_text(), "Buttons unlocked") == 0);
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
}

static void the_lock_toasts_fit_a_portrait_screen_in_large_text(void)
{
    Theme theme = *expected_theme();
    default_theme_set_text_size(&theme, TEXT_SIZE_LARGE);
    default_theme_set_orientation(&theme, ORIENTATION_PORTRAIT);
    const char *const texts[] = {"Hold Play+Power", "Buttons unlocked"};
    for (int index = 0; index < 2; index++) {
        Toast toast;
        toast_show(&toast, texts[index], 0);
        Rect area = toast_area(&toast, &theme);
        int width = text_width(theme.fonts.small, texts[index]) + 2 * theme.metrics.padding;
        CHECK(width <= area.width);
        CHECK(area.width < theme.metrics.screen_width);
    }
}

static void in_the_flipped_portrait_the_top_volume_key_is_louder_on_now_playing(void)
{
    start_app_with_library();
    play_first_album_track();
    hold_back();
    wait_ms(SLIDE_DURATION_MS);
    choose_orientation(ORIENTATION_PORTRAIT_FLIPPED);
    tap(KEY_MENU);
    wait_ms(SLIDE_DURATION_MS);
    CHECK(title_is("Now Playing"));
    uint8_t volume = fake_hal_audio_volume();
    tap(KEY_VOLUME_DOWN);
    CHECK_EQUAL(volume + 1, fake_hal_audio_volume());
}

static void in_portrait_the_volume_keys_keep_their_labels_on_now_playing(void)
{
    start_app_with_library();
    play_first_album_track();
    hold_back();
    wait_ms(SLIDE_DURATION_MS);
    choose_orientation(ORIENTATION_PORTRAIT);
    tap(KEY_MENU);
    wait_ms(SLIDE_DURATION_MS);
    CHECK(title_is("Now Playing"));
    uint8_t volume = fake_hal_audio_volume();
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(volume + 1, fake_hal_audio_volume());
}

static void holding_volume_long_after_a_step_skips_again(void)
{
    play_with_skip_gesture(SKIP_GESTURE_HOLD);
    tap(KEY_VOLUME_UP);
    wait_ms(2500);
    uint8_t volume = fake_hal_audio_volume();
    hold(KEY_VOLUME_UP);
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(volume, fake_hal_audio_volume());
}

static void holding_volume_still_repeats_it_with_the_play_clicks_setting(void)
{
    play_with_skip_gesture(SKIP_GESTURE_PLAY_CLICKS);
    uint8_t volume = fake_hal_audio_volume();
    hold(KEY_VOLUME_UP);
    CHECK(fake_hal_audio_volume() > volume);
}

static void play_clicks_skip_songs_and_a_single_click_pauses(void)
{
    play_with_skip_gesture(SKIP_GESTURE_PLAY_CLICKS);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    wait_ms(400);
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    wait_ms(400);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
}

static void volume_stays_instant_and_repeats_with_the_play_clicks_setting(void)
{
    play_with_skip_gesture(SKIP_GESTURE_PLAY_CLICKS);
    uint8_t volume = fake_hal_audio_volume();
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(volume + 1, fake_hal_audio_volume());
    hold(KEY_VOLUME_UP);
    CHECK(fake_hal_audio_volume() > volume + 1);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
}

static void holding_play_still_scrubs_with_the_play_clicks_setting(void)
{
    play_with_skip_gesture(SKIP_GESTURE_PLAY_CLICKS);
    hold(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
}

static void lists_stay_instant_with_the_click_settings(void)
{
    play_with_skip_gesture(SKIP_GESTURE_PLAY_CLICKS);
    hold_back();
    wait_ms(SLIDE_DURATION_MS);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    move_down(HOME_SETTINGS_INDEX);
    tap(KEY_PLAY);
    CHECK(title_is("Settings"));
}

static void screen_off_clicks_skip_with_the_double_volume_setting(void)
{
    play_with_skip_gesture(SKIP_GESTURE_DOUBLE_VOLUME);
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    tap(KEY_VOLUME_UP);
    tap(KEY_VOLUME_UP);
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
    CHECK(!fake_hal_display_on());
}

static void screen_off_play_clicks_skip_with_the_play_clicks_setting(void)
{
    play_with_skip_gesture(SKIP_GESTURE_PLAY_CLICKS);
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    wait_ms(400);
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
    tap(KEY_PLAY);
    wait_ms(400);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    CHECK(!fake_hal_display_on());
}

static uint16_t scrollbar_probe(void)
{
    return fake_hal_pixel(HAL_DISPLAY_WIDTH - expected_theme()->metrics.scrollbar_inset - 1, 40);
}

static void a_new_screen_slides_in_from_the_right(void)
{
    start_app();
    step();
    tap(KEY_PLAY);
    CHECK(title_is("Music"));
    CHECK(scrollbar_probe() != expected_theme()->colors.scrollbar_thumb);
    wait_ms(SLIDE_DURATION_MS / 2);
    CHECK(scrollbar_probe() != expected_theme()->colors.scrollbar_thumb);
    wait_ms(SLIDE_DURATION_MS / 2);
    CHECK_EQUAL(expected_theme()->colors.scrollbar_thumb, scrollbar_probe());
}

static void going_back_slides_the_screen_out_to_the_right(void)
{
    start_app();
    step();
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    tap(KEY_BACK);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(expected_theme()->colors.scrollbar_thumb, scrollbar_probe());
    wait_ms(SLIDE_DURATION_MS);
    CHECK(scrollbar_probe() != expected_theme()->colors.scrollbar_thumb);
}

static void waking_draws_a_frame_before_the_backlight_comes_on(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    int presents = fake_hal_present_count();
    tap(KEY_POWER);
    CHECK(fake_hal_display_on());
    CHECK(fake_hal_presents_at_power_on() > presents);
}

static void the_clock_never_changes_with_the_screen_while_audio_is_up(void)
{
    start_app_with_library();
    play_first_album_track();
    step();
    CHECK_EQUAL(200, hal_system_clock_mhz());
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    CHECK_EQUAL(200, hal_system_clock_mhz());
    tap(KEY_POWER);
    CHECK_EQUAL(200, hal_system_clock_mhz());
}

static void with_the_screen_off_the_cpu_drops_to_96_mhz_once_audio_is_down(void)
{
    start_app_with_library();
    tap(KEY_POWER);
    CHECK_EQUAL(96, hal_system_clock_mhz());
    tap(KEY_POWER);
    CHECK_EQUAL(200, hal_system_clock_mhz());
    play_first_album_track();
    tap(KEY_POWER);
    CHECK_EQUAL(200, hal_system_clock_mhz());
    fake_hal_audio_set_idle(true);
    step();
    CHECK_EQUAL(96, hal_system_clock_mhz());
    fake_hal_audio_set_idle(false);
    step();
    CHECK_EQUAL(200, hal_system_clock_mhz());
}

static void the_player_turns_itself_off_after_five_idle_minutes_on_battery(void)
{
    start_app_with_library();
    wait_ms(5 * 60000 - 1000);
    CHECK_EQUAL(0, fake_hal_power_off_count());
    wait_ms(1000);
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(1, fake_hal_power_off_count());
}

static void playing_or_usb_power_keeps_the_player_on(void)
{
    start_app_with_library();
    play_first_album_track();
    for (int minute = 0; minute < 12; minute++) {
        wait_ms(60000);
    }
    CHECK_EQUAL(0, fake_hal_power_off_count());
    tap(KEY_PLAY);
    fake_hal_set_battery(
        (HalBattery){.known = true, .percent = 80, .millivolts = 3900, .usb_power = true});
    for (int minute = 0; minute < 12; minute++) {
        wait_ms(60000);
    }
    CHECK_EQUAL(0, fake_hal_power_off_count());
}

static void the_screen_turns_off_after_the_timeout(void)
{
    start_app();
    step();
    CHECK(fake_hal_display_on());
    wait_ms(DEFAULT_TIMEOUT_MS);
    CHECK(!fake_hal_display_on());
    int presents = fake_hal_present_count();
    step();
    CHECK_EQUAL(presents, fake_hal_present_count());
}

static void after_a_timeout_volume_changes_without_waking(void)
{
    start_app_with_library();
    play_first_album_track();
    uint8_t volume = app_player_status().volume;
    wait_ms(DEFAULT_TIMEOUT_MS);
    CHECK(!fake_hal_display_on());
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(volume + 1, app_player_status().volume);
    CHECK(!fake_hal_display_on());
}

static void the_timeout_setting_is_used(void)
{
    start_app();
    open_setting(SETTINGS_TIMEOUT_INDEX);
    CHECK(title_is("Screen timeout"));
    move_up(2);
    tap(KEY_PLAY);
    wait_ms(SHORT_TIMEOUT_MS);
    CHECK(!fake_hal_display_on());
}

static void with_the_screen_off_media_keys_keep_working(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_BACK);
    CHECK(!title_is("Now Playing"));
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    uint8_t volume = app_player_status().volume;
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(volume + 1, app_player_status().volume);
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    hold(KEY_VOLUME_UP);
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
    const char *title = app_screen_title();
    tap(KEY_MENU);
    tap(KEY_BACK);
    CHECK(title_is(title));
    CHECK(!fake_hal_display_on());
    tap(KEY_POWER);
    CHECK(fake_hal_display_on());
}

static void headset(HalHeadsetCommand command)
{
    fake_hal_queue_headset_command(command);
    step();
}

static void headset_buttons_control_the_player_from_any_screen(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_BACK);
    const char *title = app_screen_title();
    uint8_t volume = app_player_status().volume;
    headset(HAL_HEADSET_VOLUME_UP);
    CHECK_EQUAL(volume + 1, app_player_status().volume);
    headset(HAL_HEADSET_VOLUME_DOWN);
    CHECK_EQUAL(volume, app_player_status().volume);
    headset(HAL_HEADSET_TOGGLE_PAUSE);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    headset(HAL_HEADSET_TOGGLE_PAUSE);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    headset(HAL_HEADSET_NEXT);
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
    headset(HAL_HEADSET_PREVIOUS);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
    CHECK(title_is(title));
}

static void headset_buttons_work_without_waking_the_screen(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    headset(HAL_HEADSET_TOGGLE_PAUSE);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    CHECK(!fake_hal_display_on());
}

static void holding_power_saves_and_turns_the_player_off(void)
{
    start_app_with_library();
    play_first_album_track();
    int writes = fake_hal_write_count();
    hold_power();
    CHECK(fake_hal_write_count() > writes);
    CHECK_EQUAL(0, fake_hal_power_off_count());
    CHECK(fake_hal_display_on());
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(1, fake_hal_power_off_count());
    CHECK(!fake_hal_display_on());
    tap(KEY_PLAY);
    CHECK_EQUAL(1, fake_hal_power_off_count());
}

static void turning_off_while_playing_silences_the_music_before_cutting_power(void)
{
    start_app_with_library();
    play_first_album_track();
    hold_power();
    CHECK_EQUAL(HAL_AUDIO_PLAYING, fake_hal_audio_state());
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(1, fake_hal_power_off_count());
    CHECK_EQUAL(HAL_AUDIO_PAUSED, fake_hal_audio_state_at_power_off());
}

static void the_sleep_timer_silences_the_music_before_cutting_power(void)
{
    start_app_with_library();
    play_first_album_track();
    hold_back();
    open_setting(SETTINGS_SLEEP_INDEX);
    CHECK(title_is("Sleep timer"));
    move_down(1);
    tap(KEY_PLAY);
    CHECK_EQUAL(HAL_AUDIO_PLAYING, fake_hal_audio_state());
    wait_ms(FIFTEEN_MINUTES_MS);
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(1, fake_hal_power_off_count());
    CHECK_EQUAL(HAL_AUDIO_PAUSED, fake_hal_audio_state_at_power_off());
}

static void a_one_second_power_hold_only_turns_the_screen_off(void)
{
    start_app_with_library();
    play_first_album_track();
    hold_for(KEY_POWER, 1000);
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(0, fake_hal_power_off_count());
    CHECK(!fake_hal_display_on());
}

static void a_slightly_long_press_wakes_the_screen_without_switching_off(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    hold_for(KEY_POWER, 1000);
    CHECK_EQUAL(0, fake_hal_power_off_count());
    CHECK(fake_hal_display_on());
}

static void a_volume_hold_under_a_second_changes_the_volume(void)
{
    start_app_with_library();
    play_first_album_track();
    uint8_t volume = fake_hal_audio_volume();
    hold_for(KEY_VOLUME_UP, 800);
    CHECK_EQUAL(volume + 1, fake_hal_audio_volume());
    CHECK(playing_path_is(HELLS_BELLS_PATH));
}

static void keys_are_ignored_while_saying_goodbye(void)
{
    start_app_with_library();
    hold_power();
    const char *title = app_screen_title();
    tap(KEY_PLAY);
    CHECK(title_is(title));
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(1, fake_hal_power_off_count());
}

static void the_splash_ignores_keys_until_it_ends(void)
{
    start_app();
    app_show_splash();
    step();
    tap(KEY_PLAY);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    wait_ms(SPLASH_MS);
    tap(KEY_PLAY);
    CHECK(title_is("Music"));
}

static void the_splash_draws_over_the_home_screen(void)
{
    start_app();
    app_show_splash();
    wait_ms(SPLASH_FADE_MS);
    Rect content = theme_content_area(expected_theme());
    CHECK_EQUAL(NOTICE_THEME.colors.background, fake_hal_pixel(content.x + 2, content.y + 4));
}

static bool toast_is(const char *expected)
{
    return app_toast_text() != NULL && strcmp(expected, app_toast_text()) == 0;
}

static void a_refused_track_says_why_it_was_skipped(void)
{
    start_app_with_library();
    fake_hal_audio_set_refusal(
        (HalAudioRefusal){.problem = HAL_AUDIO_PROBLEM_RATE, .sample_rate = 192000});
    fake_hal_audio_refuse_plays(1);
    play_first_album_track();
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
    CHECK(toast_is("192 kHz isn't supported yet"));
}

static void rescanning_without_a_card_says_so(void)
{
    start_app_with_library();
    open_setting(SETTINGS_RESCAN_INDEX);
    step();
    CHECK_EQUAL(0, fake_hal_scratch_borrow_count());
    CHECK(toast_is("No music found"));
}

static void a_rescan_of_an_empty_card_writes_an_empty_library(void)
{
    start_app_with_library();
    fake_hal_set_empty_card(true);
    app_request_rescan();
    CHECK_EQUAL(0, fake_hal_scratch_borrow_count());
    step();
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
    CHECK_EQUAL(0, fake_hal_scratch_outstanding());
    CHECK(fake_hal_library_writes() > 0);
    CHECK(toast_is("Library: 0 tracks"));
}

static void start_on_a_fresh_card(HalReserveResult result)
{
    fake_hal_reset();
    fake_hal_set_empty_card(true);
    fake_hal_set_reserve_result(result);
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    while (app_preparing()) {
        step();
    }
}

static void a_fresh_card_gets_the_player_files_and_a_first_scan(void)
{
    start_on_a_fresh_card(HAL_RESERVE_CREATED);
    CHECK_EQUAL(4096, fake_hal_reserved_size("/" FIRMWARE_DATA_DIRECTORY "/settings.bin"));
    CHECK_EQUAL(4096, fake_hal_reserved_size("/" FIRMWARE_DATA_DIRECTORY "/resume.bin"));
    CHECK_EQUAL(4096, fake_hal_reserved_size("/" FIRMWARE_DATA_DIRECTORY "/favorites.bin"));
    CHECK_EQUAL(4 * 1024 * 1024, fake_hal_reserved_size("/" FIRMWARE_DATA_DIRECTORY "/library.db"));
    uint32_t covers_size = fake_hal_reserved_size("/" FIRMWARE_DATA_DIRECTORY "/covers.bin");
    CHECK_EQUAL(80 * 1024 * 1024, covers_size);
    CHECK((covers_size - COVERS_HEADER_SIZE) / (COVERS_ENTRY_SIZE + COVER_BUILD_RECORD_BYTES) >=
          2500);
    CHECK_EQUAL(4096, fake_hal_reserved_size("/" FIRMWARE_DATA_DIRECTORY "/scan.bin"));
    CHECK_EQUAL(4096, fake_hal_reserved_size("/" FIRMWARE_DATA_DIRECTORY "/upnext.bin"));
    CHECK_EQUAL(4096, fake_hal_reserved_size("/" FIRMWARE_DATA_DIRECTORY "/bluetooth.bin"));
    CHECK_EQUAL(4096, fake_hal_reserved_size("/" FIRMWARE_DATA_DIRECTORY "/favorite_added.bin"));
    step();
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
    CHECK(toast_is("Library: 0 tracks"));
}

static void start_on_a_fresh_card_without_stepping(void)
{
    fake_hal_reset();
    fake_hal_set_empty_card(true);
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
}

static void starting_makes_no_files_before_the_first_step(void)
{
    start_on_a_fresh_card_without_stepping();
    CHECK_EQUAL(0, fake_hal_reserve_count());
    CHECK(app_preparing());
    CHECK(!fake_hal_display_on());
}

static void each_big_file_is_made_in_its_own_step_after_the_screen_is_lit(void)
{
    start_on_a_fresh_card_without_stepping();
    step();
    CHECK(fake_hal_display_on());
    CHECK(app_preparing());
    CHECK_EQUAL(1, fake_hal_reserve_count());
    step();
    CHECK_EQUAL(2, fake_hal_reserve_count());
    step();
    CHECK_EQUAL(3, fake_hal_reserve_count());
    CHECK(app_preparing());
    step();
    CHECK_EQUAL(card_files_count(), fake_hal_reserve_count());
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
}

static void a_card_with_the_files_is_prepared_within_the_first_step(void)
{
    start_app();
    CHECK(app_preparing());
    step();
    CHECK(!app_preparing());
    CHECK(fake_hal_display_on());
}

static void a_card_that_has_the_files_is_not_rescanned(void)
{
    start_on_a_fresh_card(HAL_RESERVE_EXISTS);
    CHECK_EQUAL(card_files_count(), fake_hal_reserve_count());
    step();
    CHECK_EQUAL(0, fake_hal_scratch_borrow_count());
}

static void unchanged_music_is_not_rescanned_after_usb(void)
{
    start_on_a_fresh_card(HAL_RESERVE_CREATED);
    step();
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
    app_request_rescan_if_music_changed();
    step();
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
}

static void a_new_firmware_checks_the_music_once_without_rescanning(void)
{
    start_on_a_fresh_card(HAL_RESERVE_CREATED);
    step();
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
    fake_hal_set_version("newer");
    int walks_before = fake_hal_dir_open_count();
    restart_app();
    step();
    CHECK(fake_hal_dir_open_count() > walks_before);
    CHECK_EQUAL(1, fake_hal_scratch_borrow_count());
    int walks_after = fake_hal_dir_open_count();
    restart_app();
    step();
    CHECK_EQUAL(walks_after, fake_hal_dir_open_count());
}

static void the_music_check_after_usb_says_so_before_walking_the_card(void)
{
    start_on_a_fresh_card(HAL_RESERVE_CREATED);
    step();
    int presents = fake_hal_present_count();
    app_request_rescan_if_music_changed();
    step();
    CHECK(fake_hal_presents_at_last_dir_open() > presents);
}

static void a_new_firmware_rescans_music_that_changed(void)
{
    start_on_a_fresh_card(HAL_RESERVE_CREATED);
    step();
    static const uint8_t BLANK[16] = {0};
    CHECK(hal_file_write_at("/" FIRMWARE_DATA_DIRECTORY "/scan.bin", 0, BLANK, sizeof BLANK));
    fake_hal_set_version("newer");
    restart_app();
    step();
    CHECK_EQUAL(2, fake_hal_scratch_borrow_count());
}

static void a_new_firmware_rebuilds_a_library_it_cannot_read(void)
{
    start_on_a_fresh_card(HAL_RESERVE_CREATED);
    step();
    static const uint8_t OLD_VERSION[2] = {2, 0};
    CHECK(hal_file_write_at("/" FIRMWARE_DATA_DIRECTORY "/library.db", 4, OLD_VERSION,
                            sizeof OLD_VERSION));
    fake_hal_set_version("newer");
    restart_app();
    step();
    CHECK_EQUAL(2, fake_hal_scratch_borrow_count());
}

static void a_different_stored_fingerprint_is_rescanned_after_usb(void)
{
    start_on_a_fresh_card(HAL_RESERVE_CREATED);
    step();
    static const uint8_t BLANK[16] = {0};
    CHECK(hal_file_write_at("/" FIRMWARE_DATA_DIRECTORY "/scan.bin", 0, BLANK, sizeof BLANK));
    app_request_rescan_if_music_changed();
    step();
    CHECK_EQUAL(2, fake_hal_scratch_borrow_count());
}

static void a_card_that_cannot_be_walked_is_rescanned_after_usb(void)
{
    start_on_a_fresh_card(HAL_RESERVE_CREATED);
    step();
    fake_hal_set_empty_card(false);
    app_request_rescan_if_music_changed();
    step();
    CHECK(toast_is("No music found"));
}

static void a_full_card_says_it_is_full(void)
{
    start_on_a_fresh_card(HAL_RESERVE_NO_SPACE);
    step();
    CHECK_EQUAL(0, fake_hal_scratch_borrow_count());
    CHECK(toast_is("Storage is full"));
}

static void a_rescan_after_an_installed_update_asks_for_a_restart(void)
{
    start_app_with_library();
    fake_hal_set_empty_card(true);
    fake_hal_set_restart_pending(true);
    app_request_rescan();
    step();
    CHECK_EQUAL(0, fake_hal_scratch_borrow_count());
    CHECK_EQUAL(0, fake_hal_library_writes());
    CHECK(toast_is("Restart the player to finish the update"));
}

static void the_music_check_after_an_installed_update_skips_the_walk(void)
{
    start_app_with_library();
    fake_hal_set_empty_card(true);
    fake_hal_set_restart_pending(true);
    int before = fake_hal_dir_open_count();
    app_request_rescan_if_music_changed();
    step();
    CHECK_EQUAL(before, fake_hal_dir_open_count());
    CHECK(toast_is("Restart the player to finish the update"));
}

static void a_rescan_without_scratch_memory_says_so(void)
{
    start_app_with_library();
    fake_hal_set_empty_card(true);
    fake_hal_refuse_scratch(true);
    app_request_rescan();
    step();
    fake_hal_refuse_scratch(false);
    CHECK(toast_is("Couldn't free memory for the scan"));
}

static bool rescan_message_is(ScanResult result, const char *expected)
{
    char text[48];
    RescanOutcome outcome = {.result = result};
    return strcmp(expected, library_rescan_message(outcome, text, sizeof text)) == 0;
}

static void each_way_a_scan_runs_out_has_its_own_message(void)
{
    CHECK(rescan_message_is(SCAN_OUT_OF_MEMORY, "Library too big for the player's memory"));
    CHECK(rescan_message_is(SCAN_TOO_MANY, "Too many tracks for the player"));
}

static void a_rescan_borrows_the_idle_audio_buffer_for_its_listing(void)
{
    start_app_with_library();
    fake_hal_set_empty_card(true);
    app_request_rescan();
    step();
    CHECK_EQUAL(1, fake_hal_listing_borrow_count());
}

static void a_rescan_rebuilds_the_covers_file(void)
{
    start_app_with_library();
    fake_hal_set_empty_card(true);
    app_request_rescan();
    step();
    CHECK(fake_hal_covers_writes() > 0);
    CHECK(toast_is("Library: 0 tracks"));
}

static void a_rescan_keeps_the_stall_watch_fed(void)
{
    start_app_with_library();
    fake_hal_set_empty_card(true);
    app_request_rescan();
    int before = fake_hal_keep_alive_count();
    step();
    CHECK(fake_hal_keep_alive_count() > before);
}

static void the_sleep_timer_turns_the_player_off(void)
{
    start_app_with_library();
    fake_hal_set_battery(
        (HalBattery){.known = true, .percent = 80, .millivolts = 3900, .usb_power = true});
    open_setting(SETTINGS_SLEEP_INDEX);
    CHECK(title_is("Sleep timer"));
    move_down(1);
    tap(KEY_PLAY);
    CHECK(app_toast_text() != NULL);
    wait_ms(FIFTEEN_MINUTES_MS - 1000);
    CHECK(!app_charging());
    wait_ms(1000);
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(0, fake_hal_power_off_count());
    CHECK(app_charging());
}

static const HalBattery ON_USB = {
    .known = true, .percent = 64, .millivolts = 3900, .usb_power = true, .charging = true};

static void switching_off_on_usb_power_shows_the_charge_screen(void)
{
    start_app_with_library();
    play_first_album_track();
    fake_hal_set_battery(ON_USB);
    hold_power();
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(0, fake_hal_power_off_count());
    CHECK(app_charging());
    CHECK(fake_hal_display_on());
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
}

static void the_charge_screen_goes_dark_and_a_key_lights_it(void)
{
    start_app_with_library();
    fake_hal_set_battery(ON_USB);
    hold_power();
    wait_ms(GOODBYE_MS);
    wait_ms(5000);
    CHECK(!fake_hal_display_on());
    tap(KEY_PLAY);
    CHECK(fake_hal_display_on());
    CHECK(app_charging());
}

static void charging_motion_waits_for_its_next_frame_and_stops_in_the_dark(void)
{
    start_app_with_library();
    fake_hal_set_battery(ON_USB);
    hold_power();
    wait_ms(GOODBYE_MS);
    wait_ms(1);
    int first = fake_hal_present_count();
    wait_ms(CHARGE_SCREEN_FRAME_MS - 1);
    CHECK_EQUAL(first, fake_hal_present_count());
    wait_ms(1);
    CHECK(fake_hal_present_count() > first);
    wait_ms(5000);
    CHECK(!fake_hal_display_on());
    int dark = fake_hal_present_count();
    wait_ms(1000);
    CHECK_EQUAL(dark, fake_hal_present_count());
    tap(KEY_PLAY);
    CHECK(fake_hal_display_on());
    CHECK(fake_hal_present_count() > dark);
}

static void charging_keeps_its_renderer_loaded_until_the_player_wakes(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_COVER_FLOW_INDEX);
    fake_hal_set_battery(ON_USB);
    hold_power();
    wait_ms(GOODBYE_MS);
    wait_ms(1);
    int loads = fake_hal_module_loads();
    wait_ms(20);
    CHECK_EQUAL(loads, fake_hal_module_loads());
    wait_ms(CHARGE_SCREEN_FRAME_MS);
    CHECK_EQUAL(loads, fake_hal_module_loads());
    hold_power();
    CHECK(!app_charging());
    CHECK(title_is("Cover Flow"));
}

static void a_charged_battery_does_not_keep_redrawing_an_unchanged_screen(void)
{
    start_app_with_library();
    HalBattery battery = ON_USB;
    battery.charging = false;
    battery.percent = 100;
    fake_hal_set_battery(battery);
    hold_power();
    wait_ms(GOODBYE_MS);
    wait_ms(1);
    int first = fake_hal_present_count();
    wait_ms(1000);
    CHECK_EQUAL(first, fake_hal_present_count());
}

static void menu_opens_mode_selection_from_charging_without_restarting(void)
{
    start_app_with_library();
    fake_hal_set_battery(ON_USB);
    hold_power();
    wait_ms(GOODBYE_MS);
    tap(KEY_MENU);
    CHECK(!app_charging());
    CHECK(title_is("USB mode"));
    CHECK(fake_hal_display_on());
    CHECK_EQUAL(0, fake_hal_power_off_count());
    tap(KEY_PLAY);
    CHECK(!title_is("USB mode"));
    CHECK(!app_charging());
    CHECK_EQUAL(1, fake_hal_usb_mode_requests());
    CHECK_EQUAL(HAL_USB_MODE_PLAYER, fake_hal_requested_usb_mode());
}

static void the_home_menu_opens_the_same_three_mode_selector(void)
{
    start_app_with_library();
    move_down(4);
    tap(KEY_PLAY);
    CHECK(title_is("USB mode"));
    move_down(2);
    tap(KEY_PLAY);
    CHECK_EQUAL(HAL_USB_MODE_DAC, fake_hal_usb_mode());
    CHECK_EQUAL(1, fake_hal_usb_mode_requests());
    CHECK_EQUAL(HAL_USB_MODE_DAC, fake_hal_requested_usb_mode());
}

static void storage_selection_and_return_keep_the_playback_session(void)
{
    start_app_with_library();
    play_first_album_track();
    fake_hal_audio_set_position(PLAYING_POSITION_MS);
    PlayerStatus before = app_player_status();
    app_open_usb_modes();
    CHECK(title_is("USB mode"));
    move_down(1);
    tap(KEY_PLAY);
    CHECK_EQUAL(HAL_USB_MODE_STORAGE, fake_hal_requested_usb_mode());
    app_release_audio();
    PlayerStatus after = app_player_status();
    CHECK_EQUAL(before.track, after.track);
    CHECK_EQUAL(before.position_ms, after.position_ms);
    CHECK_EQUAL(PLAYER_PAUSED, after.state);
    app_open_usb_modes();
    int depth = app_screen_depth();
    app_open_usb_modes();
    CHECK_EQUAL(depth, app_screen_depth());
    tap(KEY_PLAY);
    CHECK_EQUAL(HAL_USB_MODE_PLAYER, fake_hal_requested_usb_mode());
    restart_app();
    CHECK_EQUAL(before.track, app_player_status().track);
    CHECK_EQUAL(before.position_ms, app_player_status().position_ms);
}

static void holding_power_on_the_charge_screen_turns_the_player_on(void)
{
    start_app_with_library();
    fake_hal_set_battery(ON_USB);
    hold_power();
    wait_ms(GOODBYE_MS);
    hold_power();
    CHECK(!app_charging());
    CHECK(fake_hal_display_on());
    CHECK_EQUAL(0, fake_hal_power_off_count());
}

static void unplugging_on_the_charge_screen_turns_the_player_off(void)
{
    start_app_with_library();
    fake_hal_set_battery(ON_USB);
    hold_power();
    wait_ms(GOODBYE_MS);
    fake_hal_set_battery((HalBattery){.known = true, .percent = 64, .millivolts = 3900});
    wait_ms(500);
    CHECK_EQUAL(1, fake_hal_power_off_count());
}

static void a_cable_start_opens_the_charge_screen(void)
{
    fake_hal_reset();
    fake_hal_set_battery(ON_USB);
    fake_hal_set_usb_start(true);
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    CHECK(app_charging());
    wait_ms(100);
    CHECK(fake_hal_display_on());
    CHECK(!fake_hal_bluetooth_powered());
}

static void an_empty_battery_saves_and_turns_the_player_off(void)
{
    start_app_with_library();
    play_first_album_track();
    int writes = fake_hal_write_count();
    fake_hal_set_battery((HalBattery){.known = true, .percent = 0, .millivolts = 3400});
    wait_ms(300);
    wait_ms(1900);
    CHECK_EQUAL(0, fake_hal_power_off_count());
    wait_ms(300);
    CHECK(fake_hal_write_count() > writes);
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(1, fake_hal_power_off_count());
}

static void an_empty_battery_on_usb_power_keeps_playing(void)
{
    start_app_with_library();
    play_first_album_track();
    fake_hal_set_battery(
        (HalBattery){.known = true, .percent = 0, .millivolts = 3400, .usb_power = true});
    wait_ms(5000);
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(0, fake_hal_power_off_count());
}

static void open_albums_and_hold_the_first(void)
{
    open_music_entry(MUSIC_ALBUMS_INDEX);
    hold(KEY_PLAY);
}

static void open_first_album(void)
{
    open_music_entry(MUSIC_ALBUMS_INDEX);
    tap(KEY_PLAY);
}

static void holding_play_on_a_song_offers_up_next(void)
{
    start_app_with_library();
    open_first_album();
    hold(KEY_PLAY);
    CHECK(title_is("Hells Bells"));
    tap(KEY_BACK);
    CHECK(title_is("Back In Black"));
}

static void play_next_from_a_list_queues_the_song(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_BACK);
    move_down(1);
    hold(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK(toast_is("Playing next"));
    CHECK(title_is("Back In Black"));
    CHECK(playing_path_is(HELLS_BELLS_PATH));
}

static void add_to_up_next_from_a_list_queues_the_song(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_BACK);
    move_down(1);
    hold(KEY_PLAY);
    move_down(1);
    tap(KEY_PLAY);
    CHECK(toast_is("Added to Up Next"));
    CHECK(title_is("Back In Black"));
}

static void add_to_favorite_playlist_writes_a_playlist_file(void)
{
    start_app_with_library();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    play_first_album_track();
    tap(KEY_BACK);
    move_down(1);
    hold(KEY_PLAY);
    move_down(2);
    tap(KEY_PLAY);
    CHECK(toast_is("Added to Favorites"));
    CHECK(title_is("Back In Black"));
    uint32_t size = 0;
    const char *text = (const char *)fake_hal_file_bytes("/Favorite Tracks.m3u", &size);
    CHECK(text != NULL);
    static const char EXPECTED[] = "#EXTM3U\nAC DC/Back In Black/1-02 - Shoot to Thrill.flac\n";
    CHECK(strncmp(text, EXPECTED, sizeof EXPECTED - 1) == 0);
    CHECK(text[sizeof EXPECTED - 1] == ' ');
    hold(KEY_PLAY);
    move_down(2);
    tap(KEY_PLAY);
    CHECK(toast_is("Already in Favorites"));
}

static void open_favorite_tracks(void)
{
    tap(KEY_BACK);
    tap(KEY_BACK);
    CHECK(title_is("Music"));
    move_down(MUSIC_PLAYLISTS_INDEX - MUSIC_ALBUMS_INDEX);
    tap(KEY_PLAY);
    CHECK(title_is("Playlists"));
    move_down(1);
    tap(KEY_PLAY);
    CHECK(!title_is("Playlists"));
    CHECK(!title_is("Road Trip"));
}

static void a_favorite_shows_in_playlists_without_a_rescan(void)
{
    start_app_with_library();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    play_first_album_track();
    tap(KEY_BACK);
    move_down(1);
    hold(KEY_PLAY);
    move_down(2);
    tap(KEY_PLAY);
    CHECK(toast_is("Added to Favorites"));
    open_favorite_tracks();
    CHECK(title_is("Favorite Tracks"));
    tap(KEY_PLAY);
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
    restart_app();
    settle();
    hold_back();
    wait_ms(SLIDE_DURATION_MS);
    open_music_entry(MUSIC_PLAYLISTS_INDEX);
    move_down(1);
    tap(KEY_PLAY);
    CHECK(title_is("Favorite Tracks"));
}

static void add_to_favorite_playlist_keeps_the_cue_part_apart(void)
{
    start_app_with_cue_library();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    open_music_entry(MUSIC_ALBUMS_INDEX);
    move_down(1);
    tap(KEY_PLAY);
    CHECK(title_is("Cue Album"));
    move_down(1);
    hold(KEY_PLAY);
    move_down(2);
    tap(KEY_PLAY);
    CHECK(toast_is("Added to Favorites"));
    move_down(1);
    hold(KEY_PLAY);
    move_down(2);
    tap(KEY_PLAY);
    CHECK(toast_is("Added to Favorites"));
    move_up(1);
    hold(KEY_PLAY);
    move_down(2);
    tap(KEY_PLAY);
    CHECK(toast_is("Already in Favorites"));
    uint32_t size = 0;
    const char *text = (const char *)fake_hal_file_bytes("/Favorite Tracks.m3u", &size);
    CHECK(text != NULL);
    static const char EXPECTED[] =
        "#EXTM3U\n" PLAYLIST_START_DIRECTIVE "1000\n"
        "Cue Artist/Cue Album/Cue Album.flac\n" PLAYLIST_START_DIRECTIVE "2493\n"
        "Cue Artist/Cue Album/Cue Album.flac\n";
    CHECK(strncmp(text, EXPECTED, sizeof EXPECTED - 1) == 0);
    CHECK(text[sizeof EXPECTED - 1] == ' ');
}

static void play_next_with_nothing_playing_starts_the_song(void)
{
    start_app_with_library();
    open_first_album();
    hold(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
    CHECK(toast_is("Playing"));
}

static void menu_on_now_playing_opens_up_next(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_MENU);
    CHECK(title_is("Up Next"));
    tap(KEY_MENU);
    CHECK(title_is("Now Playing"));
    tap(KEY_MENU);
    tap(KEY_BACK);
    CHECK(title_is("Now Playing"));
}

static void choosing_a_song_in_up_next_plays_it(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_MENU);
    move_down(1);
    tap(KEY_PLAY);
    CHECK(title_is("Now Playing"));
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
}

static void holding_play_on_a_folder_plays_everything_under_it_in_order(void)
{
    start_app_with_cue_library();
    open_music_entry(MUSIC_FOLDERS_INDEX);
    hold(KEY_PLAY);
    CHECK(title_is("Now Playing"));
    CHECK_EQUAL(0, fake_hal_audio_play_start());
    CHECK_EQUAL(1000, fake_hal_audio_play_end());
    CHECK_EQUAL(1000, fake_hal_audio_queued_start());
    hold(KEY_VOLUME_UP);
    hold(KEY_VOLUME_UP);
    CHECK_EQUAL(2493, fake_hal_audio_play_start());
    hold(KEY_VOLUME_UP);
    CHECK(strcmp(CUE_AUDIO_PATH, fake_hal_audio_playing_path()) != 0);
}

static void shuffled_folder_play_starts_from_a_random_song(void)
{
    start_app_with_cue_library();
    fake_hal_set_microseconds_per_read(997);
    open_setting(SETTINGS_SHUFFLE_INDEX);
    move_down(1);
    tap(KEY_PLAY);
    tap(KEY_BACK);
    wait_ms(SLIDE_DURATION_MS);
    move_up(HOME_SETTINGS_INDEX);
    open_music_entry(MUSIC_FOLDERS_INDEX);
    bool moved = false;
    for (uint32_t attempt = 0; attempt < 6 && !moved; attempt++) {
        wait_ms(7 + attempt);
        hold(KEY_PLAY);
        CHECK(title_is("Now Playing"));
        moved = fake_hal_audio_play_start() != 0 ||
                strcmp(CUE_AUDIO_PATH, fake_hal_audio_playing_path()) != 0;
        tap(KEY_BACK);
    }
    CHECK(moved);
}

static void the_home_menu_shows_up_next_below_now_playing(void)
{
    start_app_with_library();
    play_first_album_track();
    hold_back();
    move_down(HOME_NOW_PLAYING_INDEX + 1);
    tap(KEY_PLAY);
    CHECK(title_is("Up Next"));
}

static void holding_play_on_a_folder_without_songs_shows_a_toast(void)
{
    static FixtureLibrary emptied;
    static const uint32_t FOLDERS_ENTRY = 8 + 5 * 12;
    static const uint32_t BJORK_HOMOGENIC = 7;
    fixture_library_open(&emptied);
    uint32_t folders = emptied.bytes[FOLDERS_ENTRY] | (uint32_t)emptied.bytes[FOLDERS_ENTRY + 1]
                                                          << 8;
    emptied.bytes[folders + BJORK_HOMOGENIC * 16 + 12] = 0;
    start_app_with_database(emptied.bytes, emptied.size);
    open_music_entry(MUSIC_FOLDERS_INDEX);
    move_down(2);
    hold(KEY_PLAY);
    CHECK(title_is("Folders"));
    CHECK(toast_is("No songs in this folder"));
}

static void holding_play_on_a_song_in_folders_still_opens_play_next(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_FOLDERS_INDEX);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    hold(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK(toast_is("Playing"));
}

static void holding_play_on_an_album_adds_it_to_favorites(void)
{
    start_app_with_library();
    open_albums_and_hold_the_first();
    CHECK(title_is("Albums"));
    CHECK(toast_is("Added to Favorites"));
    tap(KEY_BACK);
    move_down(MUSIC_FAVORITES_INDEX - MUSIC_ALBUMS_INDEX);
    tap(KEY_PLAY);
    CHECK(title_is("Favorites"));
    tap(KEY_PLAY);
    CHECK(title_is("Back In Black"));
}

static void holding_play_in_favorites_removes_the_album(void)
{
    start_app_with_library();
    open_albums_and_hold_the_first();
    tap(KEY_BACK);
    move_down(MUSIC_FAVORITES_INDEX - MUSIC_ALBUMS_INDEX);
    tap(KEY_PLAY);
    hold(KEY_PLAY);
    CHECK(toast_is("Removed from Favorites"));
    tap(KEY_PLAY);
    CHECK(title_is("Favorites"));
}

static void favorites_survive_a_restart(void)
{
    start_app_with_library();
    open_albums_and_hold_the_first();
    restart_app();
    open_music_entry(MUSIC_FAVORITES_INDEX);
    tap(KEY_PLAY);
    CHECK(title_is("Back In Black"));
}

static void a_toast_disappears_after_a_while(void)
{
    start_app_with_library();
    open_albums_and_hold_the_first();
    wait_ms(TOAST_DURATION_MS);
    CHECK(app_toast_text() == NULL);
}

static void playlists_open_from_the_music_menu(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_PLAYLISTS_INDEX);
    CHECK(title_is("Playlists"));
    tap(KEY_PLAY);
    CHECK(title_is("Road Trip"));
    tap(KEY_PLAY);
    CHECK(playing_path_is(SHOOTS_PATH));
}

static void start_with(StartScreen start)
{
    start_app_with_library();
    open_setting(SETTINGS_START_INDEX);
    CHECK(title_is("Start screen"));
    move_down((int)start);
    tap(KEY_PLAY);
    restart_app();
}

static void the_start_screen_setting_opens_albums(void)
{
    start_with(START_ALBUMS);
    CHECK(title_is("Albums"));
    tap(KEY_BACK);
    CHECK(title_is("Music"));
    tap(KEY_BACK);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
}

static void the_start_screen_setting_opens_now_playing(void)
{
    start_with(START_NOW_PLAYING);
    CHECK(title_is("Now Playing"));
    CHECK_EQUAL(2, app_screen_depth());
}

static void settle(void)
{
    for (int count = 0; count < SETTLE_STEPS; count++) {
        wait_ms(16);
    }
}

static bool header_title_matches_the_notice_theme(void)
{
    static Color expected[HAL_DISPLAY_WIDTH * 32];
    int height = NOTICE_THEME.metrics.header_height;
    Canvas canvas = canvas_make(expected, (Rect){0, 0, HAL_DISPLAY_WIDTH, height});
    Theme theme = NOTICE_THEME;
    theme.colors = expected_theme()->colors;
    theme.horizon = expected_theme()->horizon;
    theme.header_rule = expected_theme()->header_rule;
    header_draw(&canvas, &theme, app_screen_title());
    int inset = NOTICE_THEME.metrics.header_inset;
    for (int y = 0; y < height; y++) {
        for (int x = inset; x < HAL_DISPLAY_WIDTH - inset; x++) {
            if (fake_hal_pixel(x, y) != canvas_pixel_at(&canvas, x, y)) {
                return false;
            }
        }
    }
    return true;
}

static void after_an_installed_update_screens_use_the_resident_font(void)
{
    start_app_with_library();
    settle();
    CHECK(!header_title_matches_the_notice_theme());
    fake_hal_set_restart_pending(true);
    app_start();
    settle();
    CHECK(header_title_matches_the_notice_theme());
}

static void cover_flow_opens_from_the_music_menu(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_COVER_FLOW_INDEX);
    CHECK(title_is("Cover Flow"));
    tap(KEY_BACK);
    CHECK(title_is("Music"));
}

static void cover_flow_needs_a_library(void)
{
    start_app();
    open_music_entry(MUSIC_COVER_FLOW_INDEX);
    CHECK(title_is("No library"));
}

static void cover_flow_runs_by_album_artist_and_opens_the_centre_album(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_COVER_FLOW_INDEX);
    tap(KEY_PLAY);
    CHECK(title_is("High Voltage"));
    tap(KEY_BACK);
    move_down(1);
    tap(KEY_PLAY);
    CHECK(title_is("Back In Black"));
}

static uint16_t cover_flow_probe(void)
{
    const CoverFlowLayout *layout = &BOARD_LAYOUT.cover_flow;
    return fake_hal_pixel(layout->center_x, layout->center_top + layout->center_size / 2);
}

static void cover_flow_shows_the_centre_cover(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_COVER_FLOW_INDEX);
    settle();
    CHECK_EQUAL(BLUE_COVER, cover_flow_probe());
    move_down(1);
    settle();
    CHECK_EQUAL(RED_COVER, cover_flow_probe());
}

static void cover_flow_starts_at_the_playing_album(void)
{
    start_app_with_library();
    play_first_album_track();
    hold_back();
    tap(KEY_PLAY);
    move_up(MUSIC_ALBUMS_INDEX - MUSIC_COVER_FLOW_INDEX);
    tap(KEY_PLAY);
    CHECK(title_is("Cover Flow"));
    tap(KEY_PLAY);
    CHECK(title_is("Back In Black"));
}

static void cover_flow_starts_with_a_random_album(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_COVER_FLOW_INDEX);
    move_up(1);
    settle();
    CHECK(title_is("Cover Flow"));
    tap(KEY_PLAY);
    CHECK(title_is("Now Playing"));
    PlayerStatus status = app_player_status();
    CHECK_EQUAL(PLAYER_PLAYING, status.state);
    CHECK_EQUAL(1, status.place.number);
}

static void holding_play_in_cover_flow_toggles_a_favorite(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_COVER_FLOW_INDEX);
    hold(KEY_PLAY);
    CHECK(toast_is("Added to Favorites"));
    CHECK(title_is("Cover Flow"));
}

static bool area_has_color(Rect area, Color color)
{
    for (int y = area.y; y < area.y + area.height; y++) {
        for (int x = area.x; x < area.x + area.width; x++) {
            if (fake_hal_pixel(x, y) == color) {
                return true;
            }
        }
    }
    return false;
}

static void the_header_shows_the_play_state_and_battery(void)
{
    start_app_with_library();
    fake_hal_set_battery((HalBattery){.known = true, .percent = HALF_BATTERY, .charging = false});
    Color icon = expected_theme()->colors.header_text;
    step();
    CHECK(!area_has_color(status_bar_playback_area(expected_theme()), icon));
    CHECK(area_has_color(status_bar_battery_area(expected_theme()), icon));
    play_first_album_track();
    CHECK(area_has_color(status_bar_playback_area(expected_theme()), icon));
}

static void shuffle_shows_in_the_top_bar_as_songs_or_albums(void)
{
    start_app_with_library();
    step();
    Color icon = expected_theme()->colors.header_text;
    Rect songs = status_bar_shuffle_area(expected_theme(), STATUS_SHUFFLE_SONGS);
    CHECK(!area_has_color(songs, icon));
    open_setting(SETTINGS_SHUFFLE_INDEX);
    move_down(SHUFFLE_SONGS);
    tap(KEY_PLAY);
    CHECK(area_has_color(songs, icon));
    Rect disc_edge = {songs.x + 13, songs.y + 7, 1, 1};
    CHECK(!area_has_color(disc_edge, icon));
    tap(KEY_PLAY);
    move_down(1);
    tap(KEY_PLAY);
    CHECK(area_has_color(disc_edge, icon));
}

static void stopping_the_app_saves_the_session(void)
{
    start_app_with_library();
    play_first_album_track();
    int writes = fake_hal_write_count();
    app_stop();
    Settings settings;
    CHECK_EQUAL(writes + 2, fake_hal_write_count());
    CHECK(saved_settings(&settings));
}

static void each_step_records_its_duration(void)
{
    start_app();
    fake_hal_set_microseconds_per_read(4000);
    uint32_t before_us = fake_hal_microseconds();
    step();
    uint32_t first_read_us = before_us + 4000;
    CHECK_EQUAL(fake_hal_microseconds() - first_read_us, frame_stats_latest_us(app_frame_stats()));
    CHECK(frame_stats_latest_us(app_frame_stats()) > 0);
}

static void a_notice_is_drawn_at_once_over_the_current_screen(void)
{
    start_app();
    step();
    int presents = fake_hal_present_count();
    long pixels = fake_hal_pixels_written();
    app_show_notice("USB", "Connected to a computer");
    CHECK_EQUAL(presents + 1, fake_hal_present_count());
    CHECK(fake_hal_pixels_written() > pixels);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
}

static void a_notice_turns_the_display_on(void)
{
    start_app();
    wait_ms(DEFAULT_TIMEOUT_MS);
    CHECK(!fake_hal_display_on());
    app_show_notice("USB", "Connected to a computer");
    CHECK(fake_hal_display_on());
}

static void waking_keeps_the_screen_on_for_a_full_timeout(void)
{
    start_app();
    step();
    now_ms += DEFAULT_TIMEOUT_MS * 4;
    fake_hal_set_milliseconds(now_ms);
    app_wake();
    wait_ms(DEFAULT_TIMEOUT_MS - 1000);
    CHECK(fake_hal_display_on());
    wait_ms(1000);
    CHECK(!fake_hal_display_on());
}

static void diagnostics_shows_the_firmware_version_from_the_hal(void)
{
    start_app();
    fake_hal_set_version("1e8bebb");
    const char *value = app_diagnostics_value(DIAGNOSTICS_FIRMWARE_INDEX);
    CHECK(value != NULL);
    CHECK(strcmp("1e8bebb", value) == 0);
}

static uint16_t cover_probe(void)
{
    return fake_hal_pixel(expected_theme()->metrics.padding + 56,
                          BOARD_LAYOUT.now_playing[TEXT_SIZE_MEDIUM].cover_top + 78);
}

static void now_playing_shows_the_album_cover(void)
{
    start_app_with_library();
    play_first_album_track();
    wait_ms(SLIDE_DURATION_MS);
    step();
    CHECK_EQUAL(0xF800, cover_probe());
}

static void now_playing_without_a_cover_shows_the_cassette(void)
{
    start_app_with_library();
    open_music_entry(MUSIC_ALBUMS_INDEX);
    move_down(1);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    wait_ms(SLIDE_DURATION_MS);
    step();
    CHECK(title_is("Now Playing"));
    CHECK(cover_probe() != expected_theme()->colors.background);
}

static void an_unchanged_frame_sends_no_pixels(void)
{
    start_app();
    step();
    long pixels = fake_hal_pixels_written();
    step();
    CHECK_EQUAL(pixels, fake_hal_pixels_written());
}

static void moving_the_selection_sends_only_part_of_the_screen(void)
{
    start_app();
    step();
    long pixels = fake_hal_pixels_written();
    tap(KEY_VOLUME_UP);
    long sent = fake_hal_pixels_written() - pixels;
    CHECK(sent > 0);
    CHECK(sent < FULL_FRAME_PIXELS / 2);
}

static void restarting_the_app_sends_a_whole_frame_again(void)
{
    start_app();
    step();
    restart_app();
    long pixels = fake_hal_pixels_written();
    step();
    CHECK_EQUAL(pixels + FULL_FRAME_PIXELS, fake_hal_pixels_written());
}

static void play_on_the_playing_track_opens_now_playing_without_restarting(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_BACK);
    CHECK(!title_is("Now Playing"));
    int plays = fake_hal_audio_play_count();
    tap(KEY_PLAY);
    CHECK(title_is("Now Playing"));
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
}

static void play_on_another_track_starts_it(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_BACK);
    move_down(1);
    int plays = fake_hal_audio_play_count();
    tap(KEY_PLAY);
    CHECK(title_is("Now Playing"));
    CHECK_EQUAL(plays + 1, fake_hal_audio_play_count());
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
}

static void idling_in_the_menus_stays_put_while_playing(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_BACK);
    for (uint32_t waited = 0; waited < IDLE_MS; waited += 1000) {
        wait_ms(1000);
    }
    CHECK(fake_hal_display_on());
    CHECK(!title_is("Now Playing"));
}

static void waking_the_screen_shows_now_playing_while_playing(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_BACK);
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    tap(KEY_POWER);
    CHECK(fake_hal_display_on());
    CHECK(title_is("Now Playing"));
}

static void waking_while_paused_keeps_the_screen(void)
{
    start_app_with_library();
    play_first_album_track();
    tap(KEY_PLAY);
    tap(KEY_BACK);
    tap(KEY_POWER);
    tap(KEY_POWER);
    CHECK(!title_is("Now Playing"));
}

static void open_diagnostics_row(int index)
{
    move_down(HOME_SETTINGS_INDEX);
    tap(KEY_PLAY);
    move_down(SETTINGS_ABOUT_INDEX);
    tap(KEY_PLAY);
    move_down(index);
}

static void the_cpu_clock_cycles_through_24_96_and_200_mhz(void)
{
    start_app();
    open_diagnostics_row(DIAGNOSTICS_CPU_CLOCK_INDEX);
    CHECK_EQUAL(200, hal_system_clock_mhz());
    tap(KEY_PLAY);
    CHECK_EQUAL(24, hal_system_clock_mhz());
    tap(KEY_PLAY);
    CHECK_EQUAL(96, hal_system_clock_mhz());
    tap(KEY_PLAY);
    CHECK_EQUAL(200, hal_system_clock_mhz());
}

static void the_stall_test_row_freezes_the_main_loop_on_purpose(void)
{
    start_app();
    open_diagnostics_row(DIAGNOSTICS_STALL_TEST_INDEX);
    CHECK_EQUAL(0, fake_hal_stall_test_count());
    tap(KEY_PLAY);
    CHECK_EQUAL(1, fake_hal_stall_test_count());
}

static void the_watchdog_test_row_stops_everything_on_purpose(void)
{
    start_app();
    open_diagnostics_row(DIAGNOSTICS_WATCHDOG_TEST_INDEX);
    CHECK_EQUAL(0, fake_hal_watchdog_test_count());
    tap(KEY_PLAY);
    CHECK_EQUAL(1, fake_hal_watchdog_test_count());
}

static void ignored_power_taps_leave_the_screen_on_but_a_hold_still_switches_off(void)
{
    start_app();
    open_diagnostics_row(DIAGNOSTICS_POWER_TAPS_INDEX);
    tap(KEY_PLAY);
    tap(KEY_POWER);
    CHECK(fake_hal_display_on());
    hold_power();
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(1, fake_hal_power_off_count());
}

static void a_clock_that_does_not_lock_stays_where_it_was(void)
{
    start_app();
    fake_hal_refuse_clock_switch();
    open_diagnostics_row(DIAGNOSTICS_CPU_CLOCK_INDEX);
    tap(KEY_PLAY);
    CHECK_EQUAL(200, hal_system_clock_mhz());
    CHECK(title_is("About"));
}

static void the_profiler_row_switches_sampling_and_restarts_a_full_profile(void)
{
    start_app();
    open_diagnostics_row(DIAGNOSTICS_PROFILER_INDEX);
    CHECK(strcmp("Off", app_diagnostics_value(DIAGNOSTICS_PROFILER_INDEX)) == 0);
    tap(KEY_PLAY);
    CHECK_EQUAL(HAL_PROFILE_ON, hal_system_profile_state());
    CHECK(strcmp("On", app_diagnostics_value(DIAGNOSTICS_PROFILER_INDEX)) == 0);
    tap(KEY_PLAY);
    CHECK_EQUAL(HAL_PROFILE_OFF, hal_system_profile_state());
    fake_hal_fill_profile();
    CHECK(strcmp("Full", app_diagnostics_value(DIAGNOSTICS_PROFILER_INDEX)) == 0);
    tap(KEY_PLAY);
    CHECK_EQUAL(1, fake_hal_profile_reset_count());
    CHECK_EQUAL(HAL_PROFILE_ON, hal_system_profile_state());
}

static void about_wraps_from_profiler_to_frame_time_without_experiment_rows(void)
{
    start_app();
    open_diagnostics_row(DIAGNOSTICS_PROFILER_INDEX);
    CHECK(!overlay_visible());
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK(overlay_visible());
    CHECK(strcmp("On", app_diagnostics_value(DIAGNOSTICS_FRAME_TIME_INDEX)) == 0);
}

static void the_usb_sense_mode_is_normal_after_every_start(void)
{
    start_app();
    CHECK(!hal_system_usb_sense().kept_on);
}

static uint32_t events_of(DebugEventKind kind, uint32_t since)
{
    const DebugLog *log = debug_loop_log();
    uint32_t found = 0;
    for (uint32_t index = 0; index < debug_log_count(log); index++) {
        const DebugEvent *event = debug_log_at(log, index);
        bool fresh = debug_log_dropped(log) + index >= since;
        found += fresh && event->kind == kind ? 1 : 0;
    }
    return found;
}

static const DebugEvent *last_event(void)
{
    const DebugLog *log = debug_loop_log();
    return debug_log_at(log, debug_log_count(log) - 1);
}

static void a_key_gesture_is_logged_with_its_key_and_time(void)
{
    start_app();
    uint32_t before = debug_loop_log()->recorded;
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(1, events_of(DEBUG_EVENT_KEY, before));
    CHECK_EQUAL(DEBUG_EVENT_KEY, last_event()->kind);
    CHECK_EQUAL(KEY_VOLUME_UP | KEY_GESTURE_PRESS << 8, last_event()->value);
    CHECK_EQUAL(now_ms, last_event()->time_ms);
}

static void key_repeats_are_left_out_of_the_log(void)
{
    start_app();
    uint32_t before = debug_loop_log()->recorded;
    fake_hal_queue_transition(KEY_VOLUME_UP, true, now_ms);
    for (int count = 0; count < 20; count++) {
        wait_ms(100);
    }
    fake_hal_queue_transition(KEY_VOLUME_UP, false, now_ms);
    wait_ms(10);
    CHECK_EQUAL(1, events_of(DEBUG_EVENT_KEY, before));
}

static void a_screen_change_is_logged_with_its_depth_and_title(void)
{
    start_app();
    CHECK_EQUAL(DEBUG_EVENT_SCREEN, last_event()->kind);
    CHECK_EQUAL(1, last_event()->value);
    tap(KEY_PLAY);
    CHECK_EQUAL(DEBUG_EVENT_SCREEN, last_event()->kind);
    CHECK_EQUAL(2, last_event()->value);
    CHECK_EQUAL(0, memcmp(last_event()->text, "Music\0", 7));
    tap(KEY_BACK);
    CHECK_EQUAL(1, last_event()->value);
    CHECK_EQUAL(0, strncmp(last_event()->text, FIRMWARE_SHORT_NAME, DEBUG_LOG_TEXT_BYTES));
}

static void starting_and_pausing_a_track_is_logged_with_its_number(void)
{
    start_app_with_library();
    uint32_t before = debug_loop_log()->recorded;
    play_first_album_track();
    CHECK_EQUAL(1, events_of(DEBUG_EVENT_PLAYER, before));
    uint32_t track = app_player_status().track;
    tap(KEY_PLAY);
    CHECK_EQUAL(2, events_of(DEBUG_EVENT_PLAYER, before));
    CHECK_EQUAL(DEBUG_EVENT_PLAYER, last_event()->kind);
    CHECK_EQUAL(PLAYER_PAUSED | track << 8, last_event()->value);
}

static void new_underruns_are_logged_once_with_their_count(void)
{
    start_app();
    uint32_t before = debug_loop_log()->recorded;
    fake_hal_set_audio_stats((HalAudioStats){.underruns = 2, .output_underruns = 1});
    wait_ms(DEBUG_FOLLOW_MS);
    wait_ms(DEBUG_FOLLOW_MS);
    CHECK_EQUAL(1, events_of(DEBUG_EVENT_UNDERRUN, before));
    CHECK_EQUAL(1, events_of(DEBUG_EVENT_OUTPUT_UNDERRUN, before));
    fake_hal_set_audio_stats((HalAudioStats){.underruns = 3, .output_underruns = 1});
    wait_ms(DEBUG_FOLLOW_MS);
    CHECK_EQUAL(2, events_of(DEBUG_EVENT_UNDERRUN, before));
    CHECK_EQUAL(3, last_event()->value);
}

static void the_frame_figures_follow_the_frame_time_overlay(void)
{
    start_app();
    fake_hal_set_microseconds_per_read(100);
    for (int count = 0; count < 5; count++) {
        wait_ms(FRAME_PACER_IDLE_MS);
    }
    DebugFrames frames = app_debug_frames();
    CHECK_EQUAL(app_frame_stats()->recorded, frames.count);
    CHECK(frames.count >= 5);
    CHECK_EQUAL(frame_stats_average_us(app_frame_stats()), frames.total_us);
    CHECK_EQUAL(frame_stats_worst_us(app_frame_stats()), frames.worst_us);
    CHECK(frames.total_us >= frames.update_us + frames.draw_us + frames.send_us);
    CHECK(frames.draw_us > 0);
}

static void load_script(const char *text)
{
    DebugScriptRun *run = debug_runner_script();
    DebugScriptHost host = {hal_system_version(), hal_system_restart_pending()};
    size_t length = strlen(text);
    memcpy(run->text, text, length < DEBUG_SCRIPT_BYTES ? length : DEBUG_SCRIPT_BYTES);
    debug_script_load(run, (uint32_t)length, host);
    debug_runner_begin();
}

static void run_script_for(uint32_t duration_ms)
{
    for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += SCRIPT_STEP_MS) {
        wait_ms(SCRIPT_STEP_MS);
    }
}

static const DebugScriptRun *run_script(const char *text, uint32_t duration_ms)
{
    load_script(text);
    run_script_for(duration_ms);
    return debug_runner_script();
}

static void a_long_power_press_saves_a_report_before_the_power_goes(void)
{
    start_app();
    hold_power();
    CHECK_EQUAL(0, fake_hal_saved_report_count());
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(1, fake_hal_power_off_count());
    CHECK_EQUAL(1, fake_hal_saved_report_count());
    CHECK_EQUAL(DEBUG_REPORT_SHUTDOWN, fake_hal_saved_report(0));
}

static void switching_off_on_usb_power_saves_a_report_on_the_way_into_charge_mode(void)
{
    start_app();
    fake_hal_set_battery(ON_USB);
    wait_ms(300);
    hold_power();
    wait_ms(GOODBYE_MS);
    CHECK(app_charging());
    CHECK_EQUAL(0, fake_hal_power_off_count());
    CHECK_EQUAL(1, fake_hal_saved_report_count());
    CHECK_EQUAL(DEBUG_REPORT_SHUTDOWN, fake_hal_saved_report(0));
}

static void a_cable_start_into_charge_mode_saves_no_report(void)
{
    fake_hal_reset();
    fake_hal_set_battery(ON_USB);
    fake_hal_set_usb_start(true);
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    wait_ms(100);
    CHECK(app_charging());
    CHECK_EQUAL(0, fake_hal_saved_report_count());
}

static void unplugging_in_charge_mode_saves_a_report_before_the_power_goes(void)
{
    start_app();
    fake_hal_set_battery(ON_USB);
    wait_ms(300);
    hold_power();
    wait_ms(GOODBYE_MS);
    fake_hal_set_battery((HalBattery){.known = true, .percent = 64, .millivolts = 3900});
    wait_ms(500);
    CHECK_EQUAL(1, fake_hal_power_off_count());
    CHECK_EQUAL(2, fake_hal_saved_report_count());
    CHECK_EQUAL(DEBUG_REPORT_UNPLUGGED, fake_hal_saved_report(1));
}

static void an_empty_battery_saves_a_report_named_for_it(void)
{
    start_app();
    fake_hal_set_battery((HalBattery){.known = true, .percent = 0, .millivolts = 3400});
    for (int count = 0; count < 12; count++) {
        wait_ms(250);
    }
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(1, fake_hal_power_off_count());
    CHECK_EQUAL(DEBUG_REPORT_LOW_BATTERY, fake_hal_saved_report(0));
}

static void auto_off_names_its_report(void)
{
    start_app();
    wait_ms(6 * 60 * 1000);
    wait_ms(GOODBYE_MS);
    CHECK_EQUAL(1, fake_hal_power_off_count());
    CHECK_EQUAL(DEBUG_REPORT_AUTO_OFF, fake_hal_saved_report(0));
}

static void the_sleep_timer_names_its_report(void)
{
    start_app();
    open_setting(SETTINGS_SLEEP_INDEX);
    move_down(1);
    tap(KEY_PLAY);
    fake_hal_set_battery(ON_USB);
    wait_ms(FIFTEEN_MINUTES_MS);
    wait_ms(GOODBYE_MS);
    CHECK(fake_hal_saved_report_count() >= 1);
    CHECK_EQUAL(DEBUG_REPORT_SLEEP_TIMER, fake_hal_saved_report(0));
}

static void a_script_presses_keys_as_the_buttons_would(void)
{
    start_app();
    const DebugScriptRun *run = run_script("build any\nplay\n", 2 * SCRIPT_KEY_MS);
    CHECK(title_is("Music"));
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[0]);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[1]);
    CHECK_EQUAL(DEBUG_SCRIPT_FINISHED, run->state);
}

static void a_key_list_runs_in_order_with_long_presses(void)
{
    start_app();
    run_script("build any\nplay,play\nback:long\n", 4 * SCRIPT_KEY_MS + KEY_SCRIPT_LONG_MS);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(1, app_screen_depth());
}

static void a_script_plays_a_track(void)
{
    start_app_with_library();
    run_script("build any\nplay\ndown,down,down,down\nplay,play,play\n", 8 * SCRIPT_KEY_MS);
    CHECK(title_is("Now Playing"));
    CHECK(playing_path_is(HELLS_BELLS_PATH));
}

static void a_script_without_a_build_line_is_refused_and_still_reconnects(void)
{
    start_app();
    const DebugScriptRun *run = run_script("play\n", SCRIPT_KEY_MS);
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_NO_BUILD, run->state);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(0, fake_hal_usb_reconnect_count());
    run_script_for(DEBUG_SCRIPT_END_MS);
    CHECK_EQUAL(1, fake_hal_usb_reconnect_count());
    CHECK_EQUAL(DEBUG_LINE_NOT_RUN, run->status[0]);
}

static void a_script_whose_code_cannot_load_is_aborted_and_still_reconnects(void)
{
    start_app();
    fake_hal_break_module(DEBUG_MODULE_NUMBER, true);
    const DebugScriptRun *run = run_script("build any\nplay\n", SCRIPT_KEY_MS);
    CHECK_EQUAL(DEBUG_SCRIPT_ABORTED, run->state);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(DEBUG_LINE_NOT_RUN, run->status[1]);
    CHECK_EQUAL(0, fake_hal_usb_reconnect_count());
    run_script_for(DEBUG_SCRIPT_END_MS);
    CHECK_EQUAL(1, fake_hal_usb_reconnect_count());
    fake_hal_break_module(DEBUG_MODULE_NUMBER, false);
}

static void a_script_for_another_build_is_refused(void)
{
    start_app();
    fake_hal_set_version("1.0.0-abc1234");
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_BUILD,
                run_script("build 7777777\nplay\n", SCRIPT_KEY_MS)->state);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_BUILD,
                run_script("build 1.0.0\nplay\n", SCRIPT_KEY_MS)->state);
    fake_hal_set_version("1.0.0");
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_BUILD, run_script("build abc1234\n", SCRIPT_KEY_MS)->state);
}

static void a_script_for_this_build_runs(void)
{
    start_app();
    fake_hal_set_version("1.0.0-abc1234");
    const DebugScriptRun *run = run_script("# first\n\nbuild abc1234\nplay\n", 2 * SCRIPT_KEY_MS);
    CHECK_EQUAL(DEBUG_SCRIPT_FINISHED, run->state);
    CHECK(title_is("Music"));
}

static void a_script_waits_while_an_update_awaits_its_restart(void)
{
    start_app();
    fake_hal_set_restart_pending(true);
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_RESTART_PENDING,
                run_script("build any\nplay\n", SCRIPT_KEY_MS)->state);
    fake_hal_set_restart_pending(false);
}

static void a_script_past_the_size_or_line_limit_is_refused(void)
{
    static char text[DEBUG_SCRIPT_BYTES + 64];
    start_app();
    strcpy(text, "build any\n");
    for (int line = 0; line < DEBUG_SCRIPT_LINES; line++) {
        strcat(text, "#\n");
    }
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_TOO_LONG, run_script(text, SCRIPT_STEP_MS)->state);
    memset(text, '#', sizeof text - 1);
    memcpy(text, "build any\n", 10);
    text[sizeof text - 1] = '\0';
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_TOO_LONG, run_script(text, SCRIPT_STEP_MS)->state);
}

static void an_unknown_line_is_reported_and_the_script_goes_on(void)
{
    start_app();
    const DebugScriptRun *run =
        run_script("build any\nerase 0x03004000 1\nwait soon\nplay\n", 3 * SCRIPT_KEY_MS);
    CHECK_EQUAL(DEBUG_LINE_UNKNOWN, run->status[1]);
    CHECK_EQUAL(DEBUG_LINE_BAD_ARGUMENT, run->status[2]);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[3]);
    CHECK(title_is("Music"));
}

static void wait_holds_the_script_for_its_time(void)
{
    start_app();
    load_script("build any\nwait 500\nplay\n");
    run_script_for(400);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    run_script_for(100 + SCRIPT_KEY_MS);
    CHECK(title_is("Music"));
}

static void measure_resets_the_counters_and_records_the_window(void)
{
    start_app();
    fake_hal_set_microseconds_per_read(50);
    fake_hal_set_audio_stats((HalAudioStats){.underruns = 4, .blocks = 900});
    load_script("build any\nmeasure 400\n");
    run_script_for(4 * SCRIPT_STEP_MS);
    CHECK_EQUAL(1, fake_hal_audio_stats_reset_count());
    fake_hal_set_audio_stats((HalAudioStats){.buffered_ms = 150,
                                             .underruns = 6,
                                             .output_underruns = 1,
                                             .blocks = 10,
                                             .decode_max_us = 900,
                                             .decode_total_us = 5000,
                                             .read_max_us = 300,
                                             .read_total_us = 2000,
                                             .busy_us = 40000,
                                             .elapsed_us = 400000});
    run_script_for(420);
    const DebugScriptRun *run = debug_runner_script();
    const DebugMeasure *measure = &run->measures[0];
    CHECK_EQUAL(1, run->measure_count);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[1]);
    CHECK_EQUAL(2, measure->line);
    CHECK_EQUAL(400, measure->duration_ms);
    CHECK(measure->frames >= 3);
    CHECK(measure->frame_us >= measure->update_us + measure->draw_us + measure->send_us);
    CHECK(measure->frame_worst_us * measure->frames >= measure->frame_us);
    CHECK_EQUAL(1, measure->audio_running);
    CHECK_EQUAL(150, measure->buffered_ms);
    CHECK_EQUAL(2, measure->underruns);
    CHECK_EQUAL(1, measure->output_underruns);
    CHECK_EQUAL(10, measure->blocks);
    CHECK_EQUAL(5000, measure->handoff_us);
    CHECK_EQUAL(900, measure->handoff_max_us);
    CHECK_EQUAL(40000, measure->audio_busy_us);
    CHECK(measure->clock_us > 0);
}

static void a_fifth_measure_is_reported_as_failed(void)
{
    start_app();
    const DebugScriptRun *run =
        run_script("build any\nmeasure 8\nmeasure 8\nmeasure 8\nmeasure 8\nmeasure 8\n", 200);
    CHECK_EQUAL(DEBUG_SCRIPT_MEASURES, run->measure_count);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[4]);
    CHECK_EQUAL(DEBUG_LINE_FAILED, run->status[5]);
}

static void profile_commands_switch_and_clear_the_profiler(void)
{
    start_app();
    load_script("build any\nprofile on\nwait 100\nprofile reset\nprofile off\n");
    run_script_for(60);
    CHECK_EQUAL(HAL_PROFILE_ON, hal_system_profile_state());
    int resets = fake_hal_profile_reset_count();
    run_script_for(120);
    CHECK_EQUAL(resets + 1, fake_hal_profile_reset_count());
    CHECK_EQUAL(HAL_PROFILE_OFF, hal_system_profile_state());
}

static void usb_sense_commands_switch_the_experiment_without_hands(void)
{
    start_app();
    load_script("build any\nusb_sense kept_on\nwait 100\nusb_sense normal\n");
    run_script_for(60);
    CHECK(hal_system_usb_sense().kept_on);
    run_script_for(120);
    CHECK(!hal_system_usb_sense().kept_on);
    CHECK_EQUAL(DEBUG_LINE_OK, debug_runner_script()->status[3]);
}

static void peek_poke_and_gpio_lines_ask_the_board_and_keep_what_it_answers(void)
{
    start_app();
    fake_hal_set_probe_answer((HalProbe){true, HAL_PROBE_CABLE, 0x00000002, 0x00000003});
    const DebugScriptRun *run = run_script(
        "build any\npeek 0x500100E8\npoke 0x500100B4 0x000E0002\ngpio 0.14 function 1\n", 200);
    CHECK_EQUAL(3, fake_hal_probe_count());
    FakeProbeCall peek = fake_hal_probe_call(0);
    FakeProbeCall poke = fake_hal_probe_call(1);
    FakeProbeCall pin = fake_hal_probe_call(2);
    CHECK_EQUAL(HAL_PROBE_PEEK, peek.kind);
    CHECK_EQUAL(0x500100E8u, peek.first);
    CHECK_EQUAL(HAL_PROBE_POKE, poke.kind);
    CHECK_EQUAL(0x500100B4u, poke.first);
    CHECK_EQUAL(0x000E0002u, poke.second);
    CHECK_EQUAL(HAL_PROBE_PIN, pin.kind);
    CHECK_EQUAL(14u, pin.first);
    CHECK_EQUAL(DEBUG_PIN_FUNCTION | 1u << DEBUG_PIN_FUNCTION_SHIFT, pin.second);
    CHECK_EQUAL(3, run->probe_count);
    CHECK_EQUAL(3, run->probes[1].line);
    CHECK_EQUAL(HAL_PROBE_CABLE, run->probes[1].levels);
    CHECK_EQUAL(0x00000002u, run->probes[1].before);
    CHECK_EQUAL(0x00000003u, run->probes[1].after);
    CHECK(run->probes[2].time_ms >= run->probes[0].time_ms);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[1]);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[3]);
}

static void a_probe_the_board_refuses_is_reported_and_leaves_no_record(void)
{
    start_app();
    fake_hal_set_probe_answer((HalProbe){.allowed = false});
    const DebugScriptRun *run = run_script("build any\npoke 0x60080000 1\nwait 20\n", 200);
    CHECK_EQUAL(1, fake_hal_probe_count());
    CHECK_EQUAL(0, run->probe_count);
    CHECK_EQUAL(DEBUG_LINE_REFUSED, run->status[1]);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[2]);
    CHECK_EQUAL(DEBUG_SCRIPT_FINISHED, run->state);
}

static void a_script_keeps_sixteen_probes_and_does_not_make_a_seventeenth(void)
{
    static char text[DEBUG_SCRIPT_BYTES];
    start_app();
    fake_hal_set_probe_answer((HalProbe){.allowed = true});
    strcpy(text, "build any\n");
    for (int index = 0; index < DEBUG_SCRIPT_PROBES + 1; index++) {
        strcat(text, "gpio 2.3 read\n");
    }
    const DebugScriptRun *run = run_script(text, 600);
    CHECK_EQUAL(DEBUG_SCRIPT_PROBES, fake_hal_probe_count());
    CHECK_EQUAL(DEBUG_SCRIPT_PROBES, run->probe_count);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[DEBUG_SCRIPT_PROBES]);
    CHECK_EQUAL(DEBUG_LINE_FAILED, run->status[DEBUG_SCRIPT_PROBES + 1]);
}

static void run_script_minutes(uint32_t minutes)
{
    for (uint32_t elapsed = 0; elapsed < minutes * 60000; elapsed += 500) {
        wait_ms(500);
    }
}

static void supply_off_holds_the_usb_supply_off_until_supply_on(void)
{
    start_app();
    load_script("build any\nsupply off\nwait 200\nsupply on\nwait 200\n");
    run_script_for(100);
    CHECK(fake_hal_supply_held_off());
    run_script_for(250);
    CHECK(!fake_hal_supply_held_off());
    CHECK_EQUAL(DEBUG_LINE_OK, debug_runner_script()->status[1]);
    CHECK_EQUAL(DEBUG_LINE_OK, debug_runner_script()->status[3]);
}

static void the_supply_comes_back_when_a_script_ends_without_saying_so(void)
{
    start_app();
    const DebugScriptRun *run = run_script("build any\nsupply off\nwait 100\n", 60);
    CHECK(fake_hal_supply_held_off());
    run_script_for(200);
    CHECK_EQUAL(DEBUG_SCRIPT_FINISHED, run->state);
    CHECK(!fake_hal_supply_held_off());
}

static void the_supply_comes_back_when_a_script_is_aborted_or_times_out(void)
{
    start_app();
    load_script("build any\nsupply off\nwait 60000\n");
    run_script_for(100);
    debug_runner_abort();
    CHECK(!fake_hal_supply_held_off());
    load_script("build any\nsupply off\nwait 60000\nwait 60000\nwait 60000\nwait 60000\n"
                "wait 60000\nwait 60000\n");
    run_script_for(100);
    CHECK(fake_hal_supply_held_off());
    run_script_minutes(6);
    CHECK_EQUAL(DEBUG_SCRIPT_TIMED_OUT, debug_runner_script()->state);
    CHECK(!fake_hal_supply_held_off());
}

static void the_supply_comes_back_and_the_script_stops_when_the_player_shuts_down(void)
{
    start_app();
    load_script("build any\nsupply off\nsoak 30\n");
    run_script_for(100);
    CHECK(fake_hal_supply_held_off());
    hold_power();
    CHECK(!fake_hal_supply_held_off());
    CHECK_EQUAL(DEBUG_SCRIPT_ABORTED, debug_runner_script()->state);
}

static void a_shutdown_right_after_a_script_cancels_its_reconnect(void)
{
    start_app();
    load_script("build any\nwait 1500\n");
    fake_hal_queue_transition(KEY_POWER, true, now_ms);
    run_script_for(1800);
    CHECK_EQUAL(DEBUG_SCRIPT_FINISHED, debug_runner_script()->state);
    CHECK_EQUAL(0, fake_hal_saved_report_count());
    run_script_for(1200);
    CHECK_EQUAL(1, fake_hal_saved_report_count());
    fake_hal_queue_transition(KEY_POWER, false, now_ms);
    run_script_for(DEBUG_SCRIPT_END_MS + 1000);
    CHECK_EQUAL(0, fake_hal_usb_reconnect_count());
}

static void screen_off_darkens_the_screen_at_once_and_screen_on_lights_it(void)
{
    start_app();
    load_script("build any\nscreen off\nwait 300\nscreen on\nwait 300\n");
    run_script_for(40);
    CHECK(!fake_hal_display_on());
    run_script_for(200);
    CHECK(!fake_hal_display_on());
    run_script_for(200);
    CHECK(fake_hal_display_on());
    CHECK_EQUAL(DEBUG_LINE_OK, debug_runner_script()->status[1]);
    CHECK_EQUAL(DEBUG_LINE_OK, debug_runner_script()->status[3]);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
}

static void a_dark_screen_from_a_script_wakes_on_a_key_as_after_the_timeout(void)
{
    start_app();
    run_script("build any\nscreen off\n", 100);
    CHECK(!fake_hal_display_on());
    tap(KEY_POWER);
    wait_ms(50);
    CHECK(fake_hal_display_on());
}

static void a_soak_waits_its_minutes_and_records_both_ends(void)
{
    start_app();
    fake_hal_set_power_levels(HAL_PROBE_CABLE | HAL_PROBE_CHARGE_KNOWN | HAL_PROBE_CHARGE_PIN);
    fake_hal_set_power_samples(40);
    load_script("build any\nsupply off\nscreen off\nsoak 2\nscreen on\nwait 1000\n");
    run_script_for(100);
    const DebugScriptRun *run = debug_runner_script();
    CHECK_EQUAL(0, run->soak_count);
    uint32_t started_ms = run->soaks[0].started_ms;
    run_script_minutes(1);
    CHECK_EQUAL(0, run->soak_count);
    CHECK(!fake_hal_display_on());
    fake_hal_set_power_samples(42);
    fake_hal_set_idle_microseconds(40000000);
    fake_hal_set_power_levels(HAL_PROBE_CABLE | HAL_PROBE_CHARGE_KNOWN);
    run_script_minutes(1);
    wait_ms(500);
    CHECK_EQUAL(1, run->soak_count);
    const DebugSoak *soak = &run->soaks[0];
    CHECK_EQUAL(4, soak->line);
    CHECK_EQUAL(DEBUG_SOAK_DONE, soak->end);
    CHECK(soak->ended_ms - started_ms >= 120000 && soak->ended_ms - started_ms < 121000);
    CHECK_EQUAL(DEBUG_SOAK_CABLE | DEBUG_SOAK_CHARGE_KNOWN | DEBUG_SOAK_CHARGE_PIN |
                    DEBUG_SOAK_SUPPLY_OFF,
                soak->start.levels);
    CHECK_EQUAL(DEBUG_SOAK_CABLE | DEBUG_SOAK_CHARGE_KNOWN | DEBUG_SOAK_SUPPLY_OFF,
                soak->finish.levels);
    CHECK_EQUAL(40, soak->start.sample);
    CHECK_EQUAL(42, soak->finish.sample);
    CHECK_EQUAL(96, soak->start.clock_mhz);
    CHECK_EQUAL(96, soak->finish.clock_mhz);
    CHECK_EQUAL(40000, soak->idle_ms);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[3]);
    CHECK(fake_hal_supply_held_off());
    wait_ms(100);
    CHECK(fake_hal_display_on());
}

static void a_soak_with_the_screen_on_and_music_playing_says_so(void)
{
    start_app_with_library();
    load_script("build any\nplay\ndown,down,down,down\nplay,play,play\nsoak 1\n");
    run_script_for(8 * SCRIPT_KEY_MS);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
    run_script_minutes(1);
    wait_ms(500);
    const DebugScriptRun *run = debug_runner_script();
    CHECK_EQUAL(1, run->soak_count);
    CHECK_EQUAL(DEBUG_SOAK_DONE, run->soaks[0].end);
    CHECK_EQUAL(DEBUG_SOAK_AUDIO | DEBUG_SOAK_SCREEN, run->soaks[0].start.levels);
    CHECK_EQUAL(200, run->soaks[0].start.clock_mhz);
    CHECK(playing_path_is(HELLS_BELLS_PATH));
    CHECK((run->soaks[0].finish.levels & DEBUG_SOAK_AUDIO) != 0);
    CHECK_EQUAL(200, run->soaks[0].finish.clock_mhz);
}

static void a_soak_adds_its_minutes_to_the_scripts_time_limit_and_outlasts_auto_off(void)
{
    start_app();
    load_script("build any\nsoak 6\nwait 100\n");
    run_script_minutes(6);
    run_script_for(3000);
    const DebugScriptRun *run = debug_runner_script();
    CHECK_EQUAL(DEBUG_SCRIPT_FINISHED, run->state);
    CHECK_EQUAL(DEBUG_SOAK_DONE, run->soaks[0].end);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[2]);
    CHECK_EQUAL(0, fake_hal_power_off_count());
}

static void a_key_press_ends_a_soak_early_and_gives_the_supply_back(void)
{
    start_app();
    load_script("build any\nsupply off\nsoak 30\nwait 60000\n");
    run_script_minutes(2);
    const DebugScriptRun *run = debug_runner_script();
    CHECK_EQUAL(0, run->soak_count);
    CHECK(fake_hal_supply_held_off());
    tap(KEY_VOLUME_UP);
    wait_ms(50);
    CHECK_EQUAL(1, run->soak_count);
    CHECK_EQUAL(DEBUG_SOAK_KEY, run->soaks[0].end);
    CHECK((run->soaks[0].start.levels & DEBUG_SOAK_SUPPLY_OFF) != 0);
    CHECK((run->soaks[0].finish.levels & DEBUG_SOAK_SUPPLY_OFF) != 0);
    CHECK_EQUAL(DEBUG_LINE_CUT_SHORT, run->status[2]);
    CHECK(!fake_hal_supply_held_off());
    CHECK_EQUAL(DEBUG_SCRIPT_RUNNING, run->state);
}

static void a_key_the_script_pressed_before_a_soak_does_not_end_it(void)
{
    start_app();
    load_script("build any\nplay\nsoak 1\n");
    run_script_for(30000);
    const DebugScriptRun *run = debug_runner_script();
    CHECK_EQUAL(0, run->soak_count);
    CHECK_EQUAL(DEBUG_LINE_NOT_RUN, run->status[2]);
    CHECK(title_is("Music"));
}

static void a_battery_under_3600_millivolts_for_a_minute_ends_a_soak(void)
{
    start_app();
    load_script("build any\nsupply off\nsoak 30\n");
    fake_hal_set_battery((HalBattery){.known = true, .percent = 40, .millivolts = 3700});
    run_script_minutes(2);
    fake_hal_set_battery((HalBattery){.known = true, .percent = 20, .millivolts = 3599});
    run_script_for(40000);
    fake_hal_set_battery((HalBattery){.known = true, .percent = 21, .millivolts = 3600});
    run_script_for(40000);
    const DebugScriptRun *run = debug_runner_script();
    CHECK_EQUAL(0, run->soak_count);
    fake_hal_set_battery((HalBattery){.known = true, .percent = 20, .millivolts = 3599});
    run_script_for(55000);
    CHECK_EQUAL(0, run->soak_count);
    run_script_for(30000);
    CHECK_EQUAL(1, run->soak_count);
    CHECK_EQUAL(DEBUG_SOAK_LOW_BATTERY, run->soaks[0].end);
    CHECK_EQUAL(DEBUG_LINE_CUT_SHORT, run->status[2]);
    CHECK(!fake_hal_supply_held_off());
}

static void a_usb_session_stops_a_soak_and_the_record_says_stopped(void)
{
    start_app();
    load_script("build any\nsupply off\nsoak 30\n");
    run_script_minutes(1);
    debug_runner_abort();
    const DebugScriptRun *run = debug_runner_script();
    CHECK_EQUAL(1, run->soak_count);
    CHECK_EQUAL(DEBUG_SOAK_STOPPED, run->soaks[0].end);
    CHECK_EQUAL(DEBUG_SCRIPT_ABORTED, run->state);
    CHECK(!fake_hal_supply_held_off());
}

static void a_script_keeps_two_soaks_and_does_not_start_a_third(void)
{
    start_app();
    load_script("build any\nsoak 1\nsoak 1\nsoak 1\nwait 100\n");
    run_script_minutes(2);
    run_script_for(2000);
    const DebugScriptRun *run = debug_runner_script();
    CHECK_EQUAL(2, run->soak_count);
    CHECK_EQUAL(DEBUG_LINE_FAILED, run->status[3]);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[4]);
}

static void set_chooses_a_settings_option_as_the_screen_does(void)
{
    start_app();
    const DebugScriptRun *run =
        run_script("build any\nset Brightness=3\nset text size = large\nset Repeat=All\n", 100);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[1]);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[2]);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[3]);
    CHECK_EQUAL(3, fake_hal_display_brightness());
    CHECK_EQUAL(REPEAT_ALL, app_player_status().repeat);
    Settings saved;
    uint32_t size = 0;
    const uint8_t *bytes =
        fake_hal_internal_file_bytes("/" FIRMWARE_DATA_DIRECTORY "/settings.bin", &size);
    CHECK(bytes != NULL && settings_decode(bytes, size, &saved));
    CHECK_EQUAL(3, saved.brightness);
    CHECK_EQUAL(TEXT_SIZE_LARGE, saved.text_size);
}

static void set_refuses_what_settings_does_not_offer(void)
{
    start_app();
    const DebugScriptRun *run = run_script(
        "build any\nset Brightness=11\nset Flash=on\nset Brightness 3\nset Equalizer=#9\n", 100);
    CHECK_EQUAL(DEBUG_LINE_FAILED, run->status[1]);
    CHECK_EQUAL(DEBUG_LINE_FAILED, run->status[2]);
    CHECK_EQUAL(DEBUG_LINE_BAD_ARGUMENT, run->status[3]);
    CHECK_EQUAL(DEBUG_LINE_FAILED, run->status[4]);
}

static uint32_t little_endian(const uint8_t *bytes, int count)
{
    uint32_t value = 0;
    for (int index = count - 1; index >= 0; index--) {
        value = value << 8 | bytes[index];
    }
    return value;
}

static void a_screenshot_holds_the_frame_the_strip_renderer_draws(void)
{
    const char *path = NULL;
    uint32_t size = 0;
    start_app();
    wait_ms(SPLASH_MS);
    wait_ms(FRAME_PACER_IDLE_MS);
    const DebugScriptRun *run = run_script("build any\nscreenshot home\n", 60);
    const uint8_t *shot = fake_hal_screenshot(&path, &size);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[1]);
    CHECK_EQUAL(1, run->screenshot_count);
    CHECK_EQUAL(2, run->screenshot_lines[0]);
    CHECK(strcmp("/" FIRMWARE_DATA_DIRECTORY "/debug/shot0.565", path) == 0);
    CHECK_EQUAL(32 + FULL_FRAME_PIXELS * 2, size);
    CHECK_EQUAL(0, memcmp(shot, "DSSH\x01\x01", 6));
    CHECK_EQUAL(HAL_DISPLAY_WIDTH, little_endian(shot + 6, 2));
    CHECK_EQUAL(HAL_DISPLAY_HEIGHT, little_endian(shot + 8, 2));
    CHECK_EQUAL(32, little_endian(shot + 10, 2));
    CHECK_EQUAL(0, memcmp(shot + 16, "home\0\0\0\0\0\0\0\0", 12));
    int different = 0;
    for (int y = 0; y < HAL_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < HAL_DISPLAY_WIDTH; x++) {
            const uint8_t *pixel = shot + 32 + 2 * (y * HAL_DISPLAY_WIDTH + x);
            different += little_endian(pixel, 2) != fake_hal_pixel(x, y);
        }
    }
    CHECK_EQUAL(0, different);
}

static void a_script_takes_at_most_four_screenshots_in_numbered_files(void)
{
    const char *path = NULL;
    uint32_t size = 0;
    start_app();
    const DebugScriptRun *run = run_script(
        "build any\nscreenshot a\nscreenshot b\nscreenshot c\nscreenshot d\nscreenshot e\n", 200);
    fake_hal_screenshot(&path, &size);
    CHECK_EQUAL(DEBUG_SCRIPT_SCREENSHOTS, run->screenshot_count);
    CHECK(strcmp("/" FIRMWARE_DATA_DIRECTORY "/debug/shot3.565", path) == 0);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[4]);
    CHECK_EQUAL(DEBUG_LINE_FAILED, run->status[5]);
}

static void a_screenshot_that_cannot_be_stored_is_reported_as_failed(void)
{
    start_app();
    fake_hal_set_reserve_result(HAL_RESERVE_NO_SPACE);
    const DebugScriptRun *run = run_script("build any\nscreenshot home\n", 60);
    CHECK_EQUAL(DEBUG_LINE_FAILED, run->status[1]);
    CHECK_EQUAL(0, run->screenshot_count);
}

static void reconnect_ends_the_script_and_asks_for_usb_at_once(void)
{
    start_app();
    const DebugScriptRun *run = run_script("build any\nreconnect\nplay\n", 2 * SCRIPT_KEY_MS);
    CHECK_EQUAL(DEBUG_SCRIPT_FINISHED, run->state);
    CHECK_EQUAL(DEBUG_LINE_OK, run->status[1]);
    CHECK_EQUAL(DEBUG_LINE_NOT_RUN, run->status[2]);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(1, fake_hal_usb_reconnect_count());
    run_script_for(2 * DEBUG_SCRIPT_END_MS);
    CHECK_EQUAL(1, fake_hal_usb_reconnect_count());
}

static void a_script_that_ends_without_reconnect_reconnects_after_the_safety_time(void)
{
    start_app();
    const DebugScriptRun *run = run_script("build any\nwait 100\n", 200);
    CHECK_EQUAL(DEBUG_SCRIPT_FINISHED, run->state);
    CHECK_EQUAL(0, fake_hal_usb_reconnect_count());
    run_script_for(DEBUG_SCRIPT_END_MS);
    CHECK_EQUAL(1, fake_hal_usb_reconnect_count());
    run_script_for(DEBUG_SCRIPT_END_MS);
    CHECK_EQUAL(1, fake_hal_usb_reconnect_count());
}

static void a_script_past_its_time_limit_is_stopped_and_reconnects(void)
{
    start_app();
    load_script("build any\nwait 60000\nwait 60000\nwait 60000\nwait 60000\nwait 60000\n"
                "wait 60000\nplay\n");
    for (uint32_t elapsed = 0; elapsed < DEBUG_SCRIPT_LIMIT_MS + 1000; elapsed += 500) {
        wait_ms(500);
    }
    const DebugScriptRun *run = debug_runner_script();
    CHECK_EQUAL(DEBUG_SCRIPT_TIMED_OUT, run->state);
    CHECK_EQUAL(1, fake_hal_usb_reconnect_count());
    CHECK_EQUAL(DEBUG_LINE_NOT_RUN, run->status[6]);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
}

static void an_aborted_script_lets_go_of_the_key_it_holds(void)
{
    start_app();
    load_script("build any\ndown:held\nplay\n");
    run_script_for(1000);
    debug_runner_abort();
    step();
    const Theme *theme = expected_theme();
    int selected = 0;
    while (selected < 5 &&
           !selected_at(theme->metrics.header_height + selected * theme->metrics.row_height + 16)) {
        selected++;
    }
    CHECK(selected < 5);
    long pixels = fake_hal_pixels_written();
    uint32_t before = debug_loop_log()->recorded;
    run_script_for(2000);
    CHECK_EQUAL(DEBUG_SCRIPT_ABORTED, debug_runner_script()->state);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    CHECK_EQUAL(pixels, fake_hal_pixels_written());
    CHECK_EQUAL(0, events_of(DEBUG_EVENT_SCREEN, before));
    tap(KEY_PLAY);
    static const char *const titles[] = {"Music", "Settings", "Mixtapes", "Listening", "USB mode"};
    CHECK(selected < 5 && title_is(titles[selected]));
}

static void a_script_lights_the_screen_and_leaves_the_charge_screen_before_it_starts(void)
{
    fake_hal_reset();
    fake_hal_set_battery(ON_USB);
    fake_hal_set_usb_start(true);
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    CHECK(app_charging());
    wait_ms(CHARGE_DARK_MS);
    CHECK(!fake_hal_display_on());
    load_script("build any\nplay\n");
    run_script_for(SPLASH_MS + 3 * SCRIPT_KEY_MS);
    CHECK(!app_charging());
    CHECK(fake_hal_display_on());
    CHECK(title_is("Music"));
}

static void the_script_mark_follows_the_line_and_clears_at_the_end(void)
{
    start_app();
    load_script("build any\nwait 200\n");
    run_script_for(100);
    CHECK_EQUAL(2, fake_hal_script_mark());
    run_script_for(200);
    CHECK_EQUAL(0, fake_hal_script_mark());
}

static void script_steps_and_states_are_logged(void)
{
    start_app();
    uint32_t before = debug_loop_log()->recorded;
    run_script("build any\nwait 8\n", 100);
    CHECK_EQUAL(4, events_of(DEBUG_EVENT_SCRIPT, before));
    CHECK_EQUAL(DEBUG_SCRIPT_EVENT_STATE + DEBUG_SCRIPT_FINISHED, last_event()->value);
}

static void menu_opens_now_playing_from_anywhere_while_playing(void)
{
    start_app_with_library();
    play_first_album_track();
    hold_back();
    CHECK(title_is(FIRMWARE_SHORT_NAME));
    tap(KEY_MENU);
    CHECK(title_is("Now Playing"));
    int depth = app_screen_depth();
    tap(KEY_MENU);
    CHECK(title_is("Up Next"));
    CHECK_EQUAL(depth + 1, app_screen_depth());
}

static void menu_does_nothing_while_nothing_plays(void)
{
    start_app_with_library();
    tap(KEY_MENU);
    CHECK(title_is(FIRMWARE_SHORT_NAME));
}

static void normalization_choices_apply_and_survive_restart(void)
{
    start_app_with_library();
    open_setting(16);
    CHECK(title_is("ReplayGain"));
    move_down(2);
    tap(KEY_PLAY);
    CHECK(title_is("Settings"));
    Settings chosen;
    CHECK(saved_settings(&chosen));
    CHECK_EQUAL(REPLAY_GAIN_ALBUM, chosen.replay_gain_mode);
    CHECK(chosen.prevent_clipping);
    move_down(1);
    tap(KEY_PLAY);
    CHECK(title_is("Prevent clipping"));
    move_up(1);
    tap(KEY_PLAY);
    CHECK(saved_settings(&chosen));
    CHECK_EQUAL(REPLAY_GAIN_ALBUM, chosen.replay_gain_mode);
    CHECK(!chosen.prevent_clipping);
    restart_app();
    CHECK(saved_settings(&chosen));
    CHECK_EQUAL(REPLAY_GAIN_ALBUM, chosen.replay_gain_mode);
    CHECK(!chosen.prevent_clipping);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_notice_is_drawn_at_once_over_the_current_screen),
        TEST_CASE(a_notice_turns_the_display_on),
        TEST_CASE(waking_keeps_the_screen_on_for_a_full_timeout),
        TEST_CASE(a_step_writes_one_full_frame_and_presents_it),
        TEST_CASE(audio_gets_a_turn_after_every_strip),
        TEST_CASE(bluetooth_gets_a_turn_after_every_strip),
        TEST_CASE(an_unchanged_frame_sends_no_pixels),
        TEST_CASE(moving_the_selection_sends_only_part_of_the_screen),
        TEST_CASE(restarting_the_app_sends_a_whole_frame_again),
        TEST_CASE(steps_closer_than_the_frame_interval_draw_once),
        TEST_CASE(a_screen_that_stopped_changing_rests_until_a_key_arrives),
        TEST_CASE(a_resting_screen_is_redrawn_soon_after_what_it_shows_changes),
        TEST_CASE(a_resting_screen_asks_for_slow_passes_and_input_brings_fast_ones_back),
        TEST_CASE(the_home_menu_is_the_root),
        TEST_CASE(selecting_music_opens_the_music_menu),
        TEST_CASE(back_returns_to_the_previous_screen),
        TEST_CASE(repeated_play_on_a_listening_leaf_keeps_one_back_to_its_menu),
        TEST_CASE(a_long_back_returns_home_from_deep_screens),
        TEST_CASE(the_frame_overlay_is_off_until_enabled_in_diagnostics),
        TEST_CASE(the_frame_overlay_splits_the_frame_time),
        TEST_CASE(the_frame_overlay_splits_the_drawing),
        TEST_CASE(the_frame_overlay_shows_the_audio_numbers),
        TEST_CASE(the_frame_overlay_shows_average_and_longest_block_times),
        TEST_CASE(the_frame_overlay_shows_zero_before_any_block),
        TEST_CASE(diagnostics_shows_the_firmware_version_from_the_hal),
        TEST_CASE(albums_open_from_the_library),
        TEST_CASE(a_cue_album_lists_its_cue_tracks_and_each_plays_its_own_part),
        TEST_CASE(a_cue_backed_file_in_folders_plays_the_album_from_its_first_cue_track),
        TEST_CASE(a_cue_sheet_in_folders_opens_its_songs_and_plays_from_the_chosen_one),
        TEST_CASE(album_rows_show_a_chevron_and_track_rows_do_not),
        TEST_CASE(an_artist_leads_to_their_albums),
        TEST_CASE(back_returns_to_the_previous_list),
        TEST_CASE(folders_can_be_browsed_down_to_an_album),
        TEST_CASE(without_a_library_the_lists_explain_what_is_missing),
        TEST_CASE(holding_menu_on_a_browse_list_opens_its_view_options),
        TEST_CASE(holding_an_artist_publishes_only_that_artists_songs_before_playing),
        TEST_CASE(repeated_lyrics_and_menu_shortcuts_reuse_now_playing_depth),
        TEST_CASE(queue_actions_and_menu_shortcuts_reuse_now_playing_before_home),
        TEST_CASE(holding_back_from_mixtape_name_reaches_home),
        TEST_CASE(holding_back_from_a_busy_mixtape_save_reaches_home),
        TEST_CASE(a_song_actions_menu_opens_its_metadata_and_personal_tags),
        TEST_CASE(selecting_a_track_plays_it_and_shows_now_playing),
        TEST_CASE(play_on_the_playing_track_opens_now_playing_without_restarting),
        TEST_CASE(play_on_another_track_starts_it),
        TEST_CASE(menu_opens_now_playing_from_anywhere_while_playing),
        TEST_CASE(idling_in_the_menus_stays_put_while_playing),
        TEST_CASE(waking_the_screen_shows_now_playing_while_playing),
        TEST_CASE(waking_while_paused_keeps_the_screen),
        TEST_CASE(the_cpu_clock_cycles_through_24_96_and_200_mhz),
        TEST_CASE(the_stall_test_row_freezes_the_main_loop_on_purpose),
        TEST_CASE(the_watchdog_test_row_stops_everything_on_purpose),
        TEST_CASE(ignored_power_taps_leave_the_screen_on_but_a_hold_still_switches_off),
        TEST_CASE(a_clock_that_does_not_lock_stays_where_it_was),
        TEST_CASE(menu_does_nothing_while_nothing_plays),
        TEST_CASE(now_playing_shows_the_album_cover),
        TEST_CASE(now_playing_without_a_cover_shows_the_cassette),
        TEST_CASE(now_playing_keys_control_the_player),
        TEST_CASE(lists_still_move_as_soon_as_a_volume_key_goes_down),
        TEST_CASE(the_home_menu_shows_now_playing_last_once_a_track_is_loaded),
        TEST_CASE(the_home_menu_hides_now_playing_with_nothing_loaded),
        TEST_CASE(the_horizon_stays_inside_its_panel),
        TEST_CASE(home_art_shows_a_library_cover_when_idle),
        TEST_CASE(home_art_moves_on_and_skips_albums_without_a_cover),
        TEST_CASE(home_art_keeps_the_playing_album),
        TEST_CASE(settings_choose_shuffle_and_repeat),
        TEST_CASE(normalization_choices_apply_and_survive_restart),
        TEST_CASE(brightness_is_applied_and_saved),
        TEST_CASE(gain_is_applied),
        TEST_CASE(an_equalizer_preset_reaches_the_audio_and_survives_a_restart),
        TEST_CASE(browsing_equalizer_presets_previews_audio_but_saves_only_on_play),
        TEST_CASE(stopping_during_an_equalizer_preview_keeps_the_committed_curve),
        TEST_CASE(leaving_an_equalizer_preview_restores_the_curve_without_interrupting_music),
        TEST_CASE(home_and_shutdown_discard_uncommitted_equalizer_previews),
        TEST_CASE(custom_eq_adjusts_a_band_with_the_volume_keys),
        TEST_CASE(custom_eq_stops_at_ten_decibels),
        TEST_CASE(custom_eq_back_discards_the_audible_draft_without_saving_it),
        TEST_CASE(custom_eq_saves_the_curve_into_a_numbered_slot),
        TEST_CASE(a_saved_slot_is_listed_after_the_presets_and_restores_its_curve),
        TEST_CASE(a_chosen_slot_opens_its_editor_and_saves_back_into_it),
        TEST_CASE(the_equalizer_row_names_the_matching_slot),
        TEST_CASE(an_eq_file_is_listed_by_name_and_loaded_on_choosing),
        TEST_CASE(an_unreadable_eq_file_changes_nothing),
        TEST_CASE(saved_settings_are_restored_at_start),
        TEST_CASE(the_color_setting_recolors_the_selection_and_is_saved),
        TEST_CASE(scrolling_themes_previews_each_palette_without_saving),
        TEST_CASE(back_from_a_theme_preview_restores_the_saved_palette),
        TEST_CASE(choosing_a_preview_saves_it_and_keeps_it_after_restart),
        TEST_CASE(leaving_or_stopping_during_a_preview_keeps_the_saved_palette),
        TEST_CASE(large_text_resizes_the_open_list_at_once_and_is_saved),
        TEST_CASE(small_text_resizes_the_open_list_at_once),
        TEST_CASE(the_text_size_survives_a_restart),
        TEST_CASE(skip_songs_defaults_to_hold_and_is_saved),
        TEST_CASE(double_volume_clicks_skip_songs_and_single_clicks_change_the_volume),
        TEST_CASE(holding_volume_seeks_with_the_double_volume_setting),
        TEST_CASE(holding_volume_down_rewinds_with_the_double_volume_setting),
        TEST_CASE(holding_volume_up_fast_forwards_with_the_screen_off),
        TEST_CASE(holding_volume_down_rewinds_with_the_screen_off),
        TEST_CASE(holding_volume_still_repeats_it_with_the_play_clicks_setting),
        TEST_CASE(holding_volume_after_a_step_changes_it_with_the_hold_setting),
        TEST_CASE(holding_volume_after_a_step_changes_it_with_the_double_volume_setting),
        TEST_CASE(holding_volume_after_a_step_changes_it_with_the_screen_off),
        TEST_CASE(holding_volume_long_after_a_step_skips_again),
        TEST_CASE(play_and_power_lock_and_unlock_the_buttons),
        TEST_CASE(the_lock_toasts_fit_a_portrait_screen_in_large_text),
        TEST_CASE(in_portrait_the_volume_keys_keep_their_labels_on_now_playing),
        TEST_CASE(in_the_flipped_portrait_the_top_volume_key_is_louder_on_now_playing),
        TEST_CASE(the_lock_works_with_the_screen_off),
        TEST_CASE(play_clicks_skip_songs_and_a_single_click_pauses),
        TEST_CASE(volume_stays_instant_and_repeats_with_the_play_clicks_setting),
        TEST_CASE(holding_play_still_scrubs_with_the_play_clicks_setting),
        TEST_CASE(lists_stay_instant_with_the_click_settings),
        TEST_CASE(screen_off_clicks_skip_with_the_double_volume_setting),
        TEST_CASE(screen_off_play_clicks_skip_with_the_play_clicks_setting),
        TEST_CASE(the_home_menu_fits_its_rows_to_the_new_size_when_shown_again),
        TEST_CASE(the_orientation_setting_is_saved),
        TEST_CASE(choosing_portrait_turns_the_screen_and_redraws_it_upright),
        TEST_CASE(the_flipped_portrait_turns_the_panel_the_other_way),
        TEST_CASE(choosing_landscape_again_turns_the_screen_back),
        TEST_CASE(the_screen_starts_in_the_saved_orientation),
        TEST_CASE(portrait_home_and_lists_keep_their_title_below_the_status_row),
        TEST_CASE(in_portrait_the_volume_down_key_moves_down_the_list),
        TEST_CASE(in_the_flipped_portrait_the_rocker_keeps_its_landscape_direction),
        TEST_CASE(a_key_held_across_the_turn_keeps_the_direction_it_started_with),
        TEST_CASE(home_puts_the_album_art_under_the_menu_in_portrait),
        TEST_CASE(now_playing_centres_the_cover_under_the_status_row_in_portrait),
        TEST_CASE(cover_flow_stacks_the_albums_in_portrait),
        TEST_CASE(a_new_screen_slides_in_from_the_right),
        TEST_CASE(going_back_slides_the_screen_out_to_the_right),
        TEST_CASE(waking_draws_a_frame_before_the_backlight_comes_on),
        TEST_CASE(the_clock_never_changes_with_the_screen_while_audio_is_up),
        TEST_CASE(with_the_screen_off_the_cpu_drops_to_96_mhz_once_audio_is_down),
        TEST_CASE(the_player_turns_itself_off_after_five_idle_minutes_on_battery),
        TEST_CASE(playing_or_usb_power_keeps_the_player_on),
        TEST_CASE(the_screen_turns_off_after_the_timeout),
        TEST_CASE(after_a_timeout_volume_changes_without_waking),
        TEST_CASE(the_timeout_setting_is_used),
        TEST_CASE(with_the_screen_off_media_keys_keep_working),
        TEST_CASE(headset_buttons_control_the_player_from_any_screen),
        TEST_CASE(headset_buttons_work_without_waking_the_screen),
        TEST_CASE(holding_power_saves_and_turns_the_player_off),
        TEST_CASE(turning_off_while_playing_silences_the_music_before_cutting_power),
        TEST_CASE(the_sleep_timer_silences_the_music_before_cutting_power),
        TEST_CASE(a_one_second_power_hold_only_turns_the_screen_off),
        TEST_CASE(a_slightly_long_press_wakes_the_screen_without_switching_off),
        TEST_CASE(a_volume_hold_under_a_second_changes_the_volume),
        TEST_CASE(keys_are_ignored_while_saying_goodbye),
        TEST_CASE(the_splash_ignores_keys_until_it_ends),
        TEST_CASE(the_splash_draws_over_the_home_screen),
        TEST_CASE(the_sleep_timer_turns_the_player_off),
        TEST_CASE(a_refused_track_says_why_it_was_skipped),
        TEST_CASE(rescanning_without_a_card_says_so),
        TEST_CASE(a_rescan_of_an_empty_card_writes_an_empty_library),
        TEST_CASE(a_rescan_keeps_the_stall_watch_fed),
        TEST_CASE(a_rescan_rebuilds_the_covers_file),
        TEST_CASE(a_rescan_after_an_installed_update_asks_for_a_restart),
        TEST_CASE(after_an_installed_update_screens_use_the_resident_font),
        TEST_CASE(a_rescan_without_scratch_memory_says_so),
        TEST_CASE(a_rescan_borrows_the_idle_audio_buffer_for_its_listing),
        TEST_CASE(each_way_a_scan_runs_out_has_its_own_message),
        TEST_CASE(the_music_check_after_an_installed_update_skips_the_walk),
        TEST_CASE(a_fresh_card_gets_the_player_files_and_a_first_scan),
        TEST_CASE(starting_makes_no_files_before_the_first_step),
        TEST_CASE(each_big_file_is_made_in_its_own_step_after_the_screen_is_lit),
        TEST_CASE(a_card_with_the_files_is_prepared_within_the_first_step),
        TEST_CASE(a_card_that_has_the_files_is_not_rescanned),
        TEST_CASE(unchanged_music_is_not_rescanned_after_usb),
        TEST_CASE(a_different_stored_fingerprint_is_rescanned_after_usb),
        TEST_CASE(a_new_firmware_checks_the_music_once_without_rescanning),
        TEST_CASE(the_music_check_after_usb_says_so_before_walking_the_card),
        TEST_CASE(a_new_firmware_rescans_music_that_changed),
        TEST_CASE(a_new_firmware_rebuilds_a_library_it_cannot_read),
        TEST_CASE(a_card_that_cannot_be_walked_is_rescanned_after_usb),
        TEST_CASE(a_full_card_says_it_is_full),
        TEST_CASE(an_empty_battery_saves_and_turns_the_player_off),
        TEST_CASE(an_empty_battery_on_usb_power_keeps_playing),
        TEST_CASE(switching_off_on_usb_power_shows_the_charge_screen),
        TEST_CASE(the_charge_screen_goes_dark_and_a_key_lights_it),
        TEST_CASE(charging_motion_waits_for_its_next_frame_and_stops_in_the_dark),
        TEST_CASE(charging_keeps_its_renderer_loaded_until_the_player_wakes),
        TEST_CASE(a_charged_battery_does_not_keep_redrawing_an_unchanged_screen),
        TEST_CASE(menu_opens_mode_selection_from_charging_without_restarting),
        TEST_CASE(the_home_menu_opens_the_same_three_mode_selector),
        TEST_CASE(storage_selection_and_return_keep_the_playback_session),
        TEST_CASE(holding_power_on_the_charge_screen_turns_the_player_on),
        TEST_CASE(unplugging_on_the_charge_screen_turns_the_player_off),
        TEST_CASE(a_cable_start_opens_the_charge_screen),
        TEST_CASE(holding_play_on_a_folder_plays_everything_under_it_in_order),
        TEST_CASE(shuffled_folder_play_starts_from_a_random_song),
        TEST_CASE(the_home_menu_shows_up_next_below_now_playing),
        TEST_CASE(holding_play_on_a_folder_without_songs_shows_a_toast),
        TEST_CASE(holding_play_on_a_song_in_folders_still_opens_play_next),
        TEST_CASE(holding_play_on_an_album_adds_it_to_favorites),
        TEST_CASE(menu_on_now_playing_opens_up_next),
        TEST_CASE(choosing_a_song_in_up_next_plays_it),
        TEST_CASE(holding_play_on_a_song_offers_up_next),
        TEST_CASE(add_to_favorite_playlist_writes_a_playlist_file),
        TEST_CASE(add_to_favorite_playlist_keeps_the_cue_part_apart),
        TEST_CASE(a_favorite_shows_in_playlists_without_a_rescan),
        TEST_CASE(play_next_from_a_list_queues_the_song),
        TEST_CASE(add_to_up_next_from_a_list_queues_the_song),
        TEST_CASE(play_next_with_nothing_playing_starts_the_song),
        TEST_CASE(holding_play_in_favorites_removes_the_album),
        TEST_CASE(favorites_survive_a_restart),
        TEST_CASE(a_toast_disappears_after_a_while),
        TEST_CASE(playlists_open_from_the_music_menu),
        TEST_CASE(the_start_screen_setting_opens_albums),
        TEST_CASE(the_start_screen_setting_opens_now_playing),
        TEST_CASE(the_header_shows_the_play_state_and_battery),
        TEST_CASE(shuffle_shows_in_the_top_bar_as_songs_or_albums),
        TEST_CASE(cover_flow_opens_from_the_music_menu),
        TEST_CASE(cover_flow_needs_a_library),
        TEST_CASE(cover_flow_runs_by_album_artist_and_opens_the_centre_album),
        TEST_CASE(cover_flow_shows_the_centre_cover),
        TEST_CASE(cover_flow_starts_at_the_playing_album),
        TEST_CASE(holding_play_in_cover_flow_toggles_a_favorite),
        TEST_CASE(cover_flow_starts_with_a_random_album),
        TEST_CASE(stopping_the_app_saves_the_session),
        TEST_CASE(each_step_records_its_duration),
        TEST_CASE(the_profiler_row_switches_sampling_and_restarts_a_full_profile),
        TEST_CASE(about_wraps_from_profiler_to_frame_time_without_experiment_rows),
        TEST_CASE(the_usb_sense_mode_is_normal_after_every_start),
        TEST_CASE(a_key_gesture_is_logged_with_its_key_and_time),
        TEST_CASE(key_repeats_are_left_out_of_the_log),
        TEST_CASE(a_screen_change_is_logged_with_its_depth_and_title),
        TEST_CASE(starting_and_pausing_a_track_is_logged_with_its_number),
        TEST_CASE(new_underruns_are_logged_once_with_their_count),
        TEST_CASE(the_frame_figures_follow_the_frame_time_overlay),
        TEST_CASE(a_long_power_press_saves_a_report_before_the_power_goes),
        TEST_CASE(switching_off_on_usb_power_saves_a_report_on_the_way_into_charge_mode),
        TEST_CASE(a_cable_start_into_charge_mode_saves_no_report),
        TEST_CASE(unplugging_in_charge_mode_saves_a_report_before_the_power_goes),
        TEST_CASE(an_empty_battery_saves_a_report_named_for_it),
        TEST_CASE(auto_off_names_its_report),
        TEST_CASE(the_sleep_timer_names_its_report),
        TEST_CASE(a_script_presses_keys_as_the_buttons_would),
        TEST_CASE(a_key_list_runs_in_order_with_long_presses),
        TEST_CASE(a_script_plays_a_track),
        TEST_CASE(a_script_without_a_build_line_is_refused_and_still_reconnects),
        TEST_CASE(a_script_whose_code_cannot_load_is_aborted_and_still_reconnects),
        TEST_CASE(a_script_for_another_build_is_refused),
        TEST_CASE(a_script_for_this_build_runs),
        TEST_CASE(a_script_waits_while_an_update_awaits_its_restart),
        TEST_CASE(a_script_past_the_size_or_line_limit_is_refused),
        TEST_CASE(an_unknown_line_is_reported_and_the_script_goes_on),
        TEST_CASE(wait_holds_the_script_for_its_time),
        TEST_CASE(measure_resets_the_counters_and_records_the_window),
        TEST_CASE(a_fifth_measure_is_reported_as_failed),
        TEST_CASE(profile_commands_switch_and_clear_the_profiler),
        TEST_CASE(usb_sense_commands_switch_the_experiment_without_hands),
        TEST_CASE(peek_poke_and_gpio_lines_ask_the_board_and_keep_what_it_answers),
        TEST_CASE(a_probe_the_board_refuses_is_reported_and_leaves_no_record),
        TEST_CASE(a_script_keeps_sixteen_probes_and_does_not_make_a_seventeenth),
        TEST_CASE(supply_off_holds_the_usb_supply_off_until_supply_on),
        TEST_CASE(the_supply_comes_back_when_a_script_ends_without_saying_so),
        TEST_CASE(the_supply_comes_back_when_a_script_is_aborted_or_times_out),
        TEST_CASE(the_supply_comes_back_and_the_script_stops_when_the_player_shuts_down),
        TEST_CASE(a_shutdown_right_after_a_script_cancels_its_reconnect),
        TEST_CASE(screen_off_darkens_the_screen_at_once_and_screen_on_lights_it),
        TEST_CASE(a_dark_screen_from_a_script_wakes_on_a_key_as_after_the_timeout),
        TEST_CASE(a_soak_waits_its_minutes_and_records_both_ends),
        TEST_CASE(a_soak_with_the_screen_on_and_music_playing_says_so),
        TEST_CASE(a_soak_adds_its_minutes_to_the_scripts_time_limit_and_outlasts_auto_off),
        TEST_CASE(a_key_press_ends_a_soak_early_and_gives_the_supply_back),
        TEST_CASE(a_key_the_script_pressed_before_a_soak_does_not_end_it),
        TEST_CASE(a_battery_under_3600_millivolts_for_a_minute_ends_a_soak),
        TEST_CASE(a_usb_session_stops_a_soak_and_the_record_says_stopped),
        TEST_CASE(a_script_keeps_two_soaks_and_does_not_start_a_third),
        TEST_CASE(set_chooses_a_settings_option_as_the_screen_does),
        TEST_CASE(set_refuses_what_settings_does_not_offer),
        TEST_CASE(a_screenshot_holds_the_frame_the_strip_renderer_draws),
        TEST_CASE(a_script_takes_at_most_four_screenshots_in_numbered_files),
        TEST_CASE(a_screenshot_that_cannot_be_stored_is_reported_as_failed),
        TEST_CASE(reconnect_ends_the_script_and_asks_for_usb_at_once),
        TEST_CASE(a_script_that_ends_without_reconnect_reconnects_after_the_safety_time),
        TEST_CASE(a_script_past_its_time_limit_is_stopped_and_reconnects),
        TEST_CASE(an_aborted_script_lets_go_of_the_key_it_holds),
        TEST_CASE(a_script_lights_the_screen_and_leaves_the_charge_screen_before_it_starts),
        TEST_CASE(the_script_mark_follows_the_line_and_clears_at_the_end),
        TEST_CASE(script_steps_and_states_are_logged),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
