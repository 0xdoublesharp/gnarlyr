#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "library/lyrics.h"
#include "screen_snapshot.h"
#include "screens/lyrics_screen.h"
#include "text_spy.h"

static const char TEXT[] =
    "[00:00]Before\n[00:00.100]Intro\n[00:01]Middle\n[00:01.500]After\n[00:02.493]Closing\n";

static FixtureLibrary fixture;
static Player player;
static LyricsScreen screen;
static Theme theme;
static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];

static void start(bool cue)
{
    fake_hal_reset();
    Library *library = cue ? fixture_cue_library_open(&fixture) : fixture_library_open(&fixture);
    player_init(&player, library);
    BrowseQuery query = cue ? (BrowseQuery){.kind = BROWSE_ALBUM_TRACKS,
                                            .group = LIBRARY_ARTISTS,
                                            .subject = 1,
                                            .artist = LIBRARY_NONE}
                            : browse_root_query(BROWSE_ALL_SONGS);
    CHECK(player_play_list(&player, query, cue ? 1 : 0, 7));
    theme = BOARD_THEME;
    lyrics_screen_init(&screen, (LyricsScreenConfig){&player, library, &theme});
}

static void add_sidecar(bool uppercase)
{
    char path[PLAYER_PATH_CAPACITY];
    snprintf(path, sizeof path, "%s", fake_hal_audio_playing_path());
    char *extension = strrchr(path, '.');
    CHECK(extension != NULL);
    if (extension != NULL) {
        memcpy(extension, uppercase ? ".LRC" : ".lrc", 5);
        fake_hal_add_file(path, (const uint8_t *)TEXT, sizeof TEXT - 1);
    }
}

static bool saw(const char *text)
{
    for (int index = 0; index < text_spy_count(); index++) {
        const TextSpyEntry *entry = text_spy_entry(index);
        if (strcmp(entry->shown, text) == 0 || strcmp(entry->rest, text) == 0) {
            return true;
        }
    }
    return false;
}

static Canvas draw(void)
{
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    text_spy_reset();
    screen.screen.draw(&screen, &canvas);
    return canvas;
}

static ScreenAction key(Key button, KeyGesture gesture)
{
    return screen.screen.handle_key(&screen, (KeyEvent){.key = button, .gesture = gesture}, 100);
}

static void start_lyrics(void)
{
    start(false);
    add_sidecar(false);
    screen.screen.enter(&screen, 0);
}

static void playback_position_updates_the_visible_lyric_window(void)
{
    start_lyrics();
    fake_hal_audio_set_position(1200);
    screen.screen.update(&screen, 10);
    draw();
    CHECK(saw("Intro"));
    CHECK(saw("Middle"));
    CHECK(saw("After"));
    CHECK(!saw("Before"));
    CHECK(fake_hal_keep_alive_count() > 0);
    CHECK(fake_hal_audio_service_count() > 0);
}

static void lyrics_read_again_after_their_work_was_lent_to_the_storage_cache(void)
{
    start_lyrics();
    fake_hal_audio_set_position(1200);
    screen.screen.update(&screen, 10);
    uint32_t *lent = lyrics_screen_lend_work();
    for (uint32_t index = 0; index < LYRICS_LENT_SECTORS * 128; index++) {
        lent[index] = 0xFFFFFFFFu;
    }
    lyrics_screen_lend_work();
    screen.screen.update(&screen, 20);
    draw();
    CHECK(saw("Intro"));
    CHECK(saw("Middle"));
    CHECK(saw("After"));
}

static void an_uppercase_sidecar_is_found_and_can_be_read_again(void)
{
    start(false);
    add_sidecar(true);
    screen.screen.enter(&screen, 0);
    fake_hal_audio_set_position(1600);
    screen.screen.update(&screen, 10);
    draw();
    CHECK(saw("After"));
    CHECK(!saw(i18n_text(STR_NO_LYRICS)));
}

static void cue_lyrics_follow_file_time_rather_than_chapter_time(void)
{
    start(true);
    add_sidecar(false);
    screen.screen.enter(&screen, 0);
    draw();
    CHECK_EQUAL(0, player_status(&player).position_ms);
    CHECK(saw("Middle"));
    CHECK(saw("After"));
    CHECK(!saw("Before"));
}

static void scrolling_is_bounded_and_long_play_restores_automatic_follow(void)
{
    start_lyrics();
    int plays = fake_hal_audio_play_count();
    for (int index = 0; index < 2 * LYRICS_LINE_CAPACITY; index++) {
        key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    }
    draw();
    CHECK(saw("Closing"));
    CHECK(screen.scroll < LYRICS_LINE_CAPACITY);
    for (int index = 0; index < 4 * LYRICS_LINE_CAPACITY; index++) {
        key(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS);
    }
    draw();
    CHECK(saw("Before"));
    CHECK(screen.scroll > -LYRICS_LINE_CAPACITY);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(0, screen.scroll);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
    CHECK_EQUAL(PLAYER_PLAYING, player_status(&player).state);
}

static void selection_pauses_and_back_or_home_do_not_restart_playback(void)
{
    start_lyrics();
    int plays = fake_hal_audio_play_count();
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(PLAYER_PAUSED, player_status(&player).state);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(PLAYER_PLAYING, player_status(&player).state);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(SCREEN_ACTION_POP_TO_ROOT, key(KEY_BACK, KEY_GESTURE_LONG_PRESS).type);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
}

static void changing_tracks_clears_the_previous_tracks_lyrics_and_scroll(void)
{
    start_lyrics();
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    CHECK(player_play_list(&player, browse_root_query(BROWSE_ALL_SONGS), 1, 7));
    screen.screen.update(&screen, 20);
    draw();
    CHECK_EQUAL(0, screen.scroll);
    CHECK(saw(i18n_text(STR_NO_LYRICS)));
    CHECK(!saw("Before"));
}

static void a_read_failure_after_indexing_does_not_show_blank_cached_lyrics(void)
{
    start_lyrics();
    fake_hal_fail_file_reads(screen.path);
    fake_hal_audio_set_position(1200);
    screen.screen.update(&screen, 20);
    draw();
    CHECK(saw(i18n_text(STR_NO_LYRICS)));
}

static void reentering_the_screen_recovers_from_a_temporary_read_failure(void)
{
    start_lyrics();
    fake_hal_fail_file_reads(screen.path);
    fake_hal_audio_set_position(1200);
    screen.screen.update(&screen, 20);
    fake_hal_fail_file_reads(NULL);
    screen.screen.enter(&screen, 30);
    draw();
    CHECK(saw("Middle"));
    CHECK(!saw(i18n_text(STR_NO_LYRICS)));
}

static void loaded_lyrics_and_read_errors_render_at_every_size_and_orientation(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            start_lyrics();
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            fake_hal_audio_set_position(1200);
            screen.screen.update(&screen, 10);
            Canvas canvas = draw();
            CHECK(saw("Middle"));
            screen_snapshot_write(&canvas, "lyrics", orientation * TEXT_SIZE_COUNT + size);
            fake_hal_fail_file_reads(screen.path);
            fake_hal_audio_set_position(1600);
            screen.screen.update(&screen, 20);
            canvas = draw();
            CHECK(saw(i18n_text(STR_NO_LYRICS)));
            screen_snapshot_write(&canvas, "lyrics-error", orientation * TEXT_SIZE_COUNT + size);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(playback_position_updates_the_visible_lyric_window),
        TEST_CASE(lyrics_read_again_after_their_work_was_lent_to_the_storage_cache),
        TEST_CASE(an_uppercase_sidecar_is_found_and_can_be_read_again),
        TEST_CASE(cue_lyrics_follow_file_time_rather_than_chapter_time),
        TEST_CASE(scrolling_is_bounded_and_long_play_restores_automatic_follow),
        TEST_CASE(selection_pauses_and_back_or_home_do_not_restart_playback),
        TEST_CASE(changing_tracks_clears_the_previous_tracks_lyrics_and_scroll),
        TEST_CASE(a_read_failure_after_indexing_does_not_show_blank_cached_lyrics),
        TEST_CASE(reentering_the_screen_recovers_from_a_temporary_read_failure),
        TEST_CASE(loaded_lyrics_and_read_errors_render_at_every_size_and_orientation),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
