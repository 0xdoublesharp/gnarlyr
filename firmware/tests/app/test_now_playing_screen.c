#include <string.h>

#include "check.h"
#include "core/frame_pacer.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "hal/display.h"
#include "screens/now_playing_screen.h"

enum {
    TEXT_CAPACITY = 48
};

static const HalAudioFormat HI_RES_FLAC = {.codec = HAL_AUDIO_CODEC_FLAC,
                                           .sample_rate = 96000,
                                           .bits_per_sample = 24,
                                           .channels = 2,
                                           .bitrate_kbps = 2304};
static const HalAudioFormat MP3 = {
    .codec = HAL_AUDIO_CODEC_MP3, .sample_rate = 44100, .channels = 2, .bitrate_kbps = 320};

static bool stream_text_is(HalAudioFormat format, const char *expected)
{
    char buffer[TEXT_CAPACITY];
    now_playing_stream_text(format, buffer, sizeof buffer);
    return strcmp(expected, buffer) == 0;
}

static bool detail_text_is(PlayerStatus status, const char *expected)
{
    char buffer[TEXT_CAPACITY];
    now_playing_detail_text(&status, buffer, sizeof buffer);
    return strcmp(expected, buffer) == 0;
}

static NowPlayingCommand command(Key key, KeyGesture gesture)
{
    return now_playing_command_for((KeyEvent){.key = key, .gesture = gesture}, (NowPlayingKeys){0});
}

static NowPlayingCommand command_with(Key key, KeyGesture gesture, SkipGesture skip)
{
    return now_playing_command_for((KeyEvent){.key = key, .gesture = gesture},
                                   (NowPlayingKeys){.skip = skip});
}

static NowPlayingCommand scrub_command(Key key, KeyGesture gesture)
{
    return now_playing_command_for((KeyEvent){.key = key, .gesture = gesture},
                                   (NowPlayingKeys){.scrubbing = true});
}

static void a_double_volume_click_skips_with_the_double_volume_setting(void)
{
    SkipGesture skip = SKIP_GESTURE_DOUBLE_VOLUME;
    CHECK_EQUAL(NOW_PLAYING_NEXT, command_with(KEY_VOLUME_UP, KEY_GESTURE_DOUBLE_PRESS, skip));
    CHECK_EQUAL(NOW_PLAYING_PREVIOUS,
                command_with(KEY_VOLUME_DOWN, KEY_GESTURE_DOUBLE_PRESS, skip));
}

static NowPlayingCommand hold_with_repeats(Key key, KeyGesture gesture, uint8_t repeats)
{
    return now_playing_command_for((KeyEvent){.key = key, .gesture = gesture, .repeats = repeats},
                                   (NowPlayingKeys){.skip = SKIP_GESTURE_DOUBLE_VOLUME});
}

static NowPlayingCommand adjusting_command(Key key, KeyGesture gesture, SkipGesture skip)
{
    return now_playing_command_for((KeyEvent){.key = key, .gesture = gesture},
                                   (NowPlayingKeys){.adjusting_volume = true, .skip = skip});
}

static void holding_volume_while_it_is_shown_changes_it(void)
{
    for (int skip = 0; skip < SKIP_GESTURE_COUNT; skip++) {
        CHECK_EQUAL(NOW_PLAYING_VOLUME_UP,
                    adjusting_command(KEY_VOLUME_UP, KEY_GESTURE_LONG_PRESS, (SkipGesture)skip));
        CHECK_EQUAL(NOW_PLAYING_VOLUME_DOWN,
                    adjusting_command(KEY_VOLUME_DOWN, KEY_GESTURE_REPEAT, (SkipGesture)skip));
    }
    CHECK_EQUAL(NOW_PLAYING_NEXT, adjusting_command(KEY_VOLUME_UP, KEY_GESTURE_DOUBLE_PRESS,
                                                    SKIP_GESTURE_DOUBLE_VOLUME));
    CHECK(now_playing_adjusting_volume(1000, 1000 + NOW_PLAYING_VOLUME_HOLD_MS - 1));
    CHECK(!now_playing_adjusting_volume(1000, 1000 + NOW_PLAYING_VOLUME_HOLD_MS));
}

static void holding_volume_seeks_with_the_double_volume_setting(void)
{
    CHECK_EQUAL(NOW_PLAYING_SEEK_FORWARD,
                hold_with_repeats(KEY_VOLUME_UP, KEY_GESTURE_LONG_PRESS, 1));
    CHECK_EQUAL(NOW_PLAYING_SEEK_BACK, hold_with_repeats(KEY_VOLUME_DOWN, KEY_GESTURE_REPEAT, 2));
    CHECK_EQUAL(
        NOW_PLAYING_SEEK_FORWARD_FAST,
        hold_with_repeats(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, NOW_PLAYING_SEEK_FAST_AFTER_REPEATS));
    CHECK_EQUAL(NOW_PLAYING_SEEK_BACK_FAST,
                hold_with_repeats(KEY_VOLUME_DOWN, KEY_GESTURE_REPEAT, 200));
}

static void play_clicks_skip_only_with_the_play_clicks_setting(void)
{
    SkipGesture skip = SKIP_GESTURE_PLAY_CLICKS;
    CHECK_EQUAL(NOW_PLAYING_NEXT, command_with(KEY_PLAY, KEY_GESTURE_DOUBLE_PRESS, skip));
    CHECK_EQUAL(NOW_PLAYING_PREVIOUS, command_with(KEY_PLAY, KEY_GESTURE_TRIPLE_PRESS, skip));
    CHECK_EQUAL(NOW_PLAYING_VOLUME_UP, command_with(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, skip));
    CHECK_EQUAL(NOW_PLAYING_NONE,
                command_with(KEY_PLAY, KEY_GESTURE_DOUBLE_PRESS, SKIP_GESTURE_HOLD));
}

static void holding_volume_skips_only_with_the_hold_setting(void)
{
    CHECK_EQUAL(NOW_PLAYING_NEXT, command(KEY_VOLUME_UP, KEY_GESTURE_LONG_PRESS));
    CHECK_EQUAL(NOW_PLAYING_NONE, command(KEY_VOLUME_UP, KEY_GESTURE_REPEAT));
}

static void a_volume_tap_changes_the_volume(void)
{
    CHECK_EQUAL(NOW_PLAYING_VOLUME_UP, command(KEY_VOLUME_UP, KEY_GESTURE_PRESS));
    CHECK_EQUAL(NOW_PLAYING_VOLUME_DOWN, command(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS));
}

static void holding_volume_up_skips_forward_and_volume_down_skips_back(void)
{
    CHECK_EQUAL(NOW_PLAYING_NEXT, command(KEY_VOLUME_UP, KEY_GESTURE_LONG_PRESS));
    CHECK_EQUAL(NOW_PLAYING_PREVIOUS, command(KEY_VOLUME_DOWN, KEY_GESTURE_LONG_PRESS));
    CHECK_EQUAL(NOW_PLAYING_NONE, command(KEY_VOLUME_UP, KEY_GESTURE_REPEAT));
    CHECK_EQUAL(NOW_PLAYING_NONE, command(KEY_VOLUME_DOWN, KEY_GESTURE_REPEAT));
}

static void play_toggles_pause_and_a_long_play_starts_seeking(void)
{
    CHECK_EQUAL(NOW_PLAYING_TOGGLE_PAUSE, command(KEY_PLAY, KEY_GESTURE_PRESS));
    CHECK_EQUAL(NOW_PLAYING_SCRUB_START, command(KEY_PLAY, KEY_GESTURE_LONG_PRESS));
}

static void while_seeking_volume_keys_move_the_target(void)
{
    CHECK_EQUAL(NOW_PLAYING_SCRUB_FORWARD, scrub_command(KEY_VOLUME_UP, KEY_GESTURE_PRESS));
    CHECK_EQUAL(NOW_PLAYING_SCRUB_FORWARD_FAST,
                scrub_command(KEY_VOLUME_UP, KEY_GESTURE_LONG_PRESS));
    CHECK_EQUAL(NOW_PLAYING_SCRUB_FORWARD_FAST, scrub_command(KEY_VOLUME_UP, KEY_GESTURE_REPEAT));
    CHECK_EQUAL(NOW_PLAYING_SCRUB_BACK, scrub_command(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS));
    CHECK_EQUAL(NOW_PLAYING_SCRUB_BACK_FAST,
                scrub_command(KEY_VOLUME_DOWN, KEY_GESTURE_LONG_PRESS));
    CHECK_EQUAL(NOW_PLAYING_SCRUB_BACK_FAST, scrub_command(KEY_VOLUME_DOWN, KEY_GESTURE_REPEAT));
}

static void while_seeking_play_commits_and_back_cancels(void)
{
    CHECK_EQUAL(NOW_PLAYING_SCRUB_COMMIT, scrub_command(KEY_PLAY, KEY_GESTURE_PRESS));
    CHECK_EQUAL(NOW_PLAYING_SCRUB_CANCEL, scrub_command(KEY_BACK, KEY_GESTURE_PRESS));
    CHECK_EQUAL(NOW_PLAYING_HOME, scrub_command(KEY_BACK, KEY_GESTURE_LONG_PRESS));
    CHECK_EQUAL(NOW_PLAYING_NONE, scrub_command(KEY_MENU, KEY_GESTURE_PRESS));
}

static void the_seek_target_stays_inside_the_track(void)
{
    CHECK_EQUAL(65000, now_playing_scrub_target(60000, 200000, NOW_PLAYING_SCRUB_STEP_MS));
    CHECK_EQUAL(0, now_playing_scrub_target(3000, 200000, -NOW_PLAYING_SCRUB_STEP_MS));
    CHECK_EQUAL(200000, now_playing_scrub_target(198000, 200000, NOW_PLAYING_SCRUB_FAST_STEP_MS));
}

static void menu_opens_quick_settings(void)
{
    CHECK_EQUAL(NOW_PLAYING_QUICK_SETTINGS, command(KEY_MENU, KEY_GESTURE_PRESS));
    CHECK_EQUAL(NOW_PLAYING_LYRICS, command(KEY_MENU, KEY_GESTURE_LONG_PRESS));
}

static void back_leaves_and_a_long_back_goes_home(void)
{
    CHECK_EQUAL(NOW_PLAYING_LEAVE, command(KEY_BACK, KEY_GESTURE_PRESS));
    CHECK_EQUAL(NOW_PLAYING_HOME, command(KEY_BACK, KEY_GESTURE_LONG_PRESS));
}

static void power_is_left_to_the_system(void)
{
    CHECK_EQUAL(NOW_PLAYING_NONE, command(KEY_POWER, KEY_GESTURE_PRESS));
    CHECK_EQUAL(NOW_PLAYING_NONE, command(KEY_POWER, KEY_GESTURE_LONG_PRESS));
}

static void volume_changes_show_the_volume_bar(void)
{
    CHECK_EQUAL(NOW_PLAYING_FEEDBACK_VOLUME,
                now_playing_feedback_for(NOW_PLAYING_VOLUME_UP, PLAYER_PLAYING));
    CHECK_EQUAL(NOW_PLAYING_FEEDBACK_VOLUME,
                now_playing_feedback_for(NOW_PLAYING_VOLUME_DOWN, PLAYER_PAUSED));
}

static void skips_and_pauses_show_a_badge_for_the_new_state(void)
{
    CHECK_EQUAL(NOW_PLAYING_FEEDBACK_NEXT,
                now_playing_feedback_for(NOW_PLAYING_NEXT, PLAYER_PLAYING));
    CHECK_EQUAL(NOW_PLAYING_FEEDBACK_PREVIOUS,
                now_playing_feedback_for(NOW_PLAYING_PREVIOUS, PLAYER_PLAYING));
    CHECK_EQUAL(NOW_PLAYING_FEEDBACK_PAUSE,
                now_playing_feedback_for(NOW_PLAYING_TOGGLE_PAUSE, PLAYER_PAUSED));
    CHECK_EQUAL(NOW_PLAYING_FEEDBACK_PLAY,
                now_playing_feedback_for(NOW_PLAYING_TOGGLE_PAUSE, PLAYER_PLAYING));
    CHECK_EQUAL(NOW_PLAYING_FEEDBACK_NONE,
                now_playing_feedback_for(NOW_PLAYING_LEAVE, PLAYER_PLAYING));
    CHECK_EQUAL(NOW_PLAYING_FEEDBACK_NONE,
                now_playing_feedback_for(NOW_PLAYING_SCRUB_START, PLAYER_PLAYING));
}

static void the_volume_bar_stays_longer_than_a_badge(void)
{
    uint32_t end = 1000 + NOW_PLAYING_VOLUME_FEEDBACK_MS;
    CHECK(now_playing_feedback_visible(NOW_PLAYING_FEEDBACK_VOLUME, 1000, end - 1));
    CHECK(!now_playing_feedback_visible(NOW_PLAYING_FEEDBACK_VOLUME, 1000, end));
    CHECK(NOW_PLAYING_VOLUME_FEEDBACK_MS >= NOW_PLAYING_VOLUME_HOLD_MS);
    CHECK(now_playing_feedback_visible(NOW_PLAYING_FEEDBACK_NEXT, 1000, 1699));
    CHECK(!now_playing_feedback_visible(NOW_PLAYING_FEEDBACK_NEXT, 1000, 1700));
    CHECK(!now_playing_feedback_visible(NOW_PLAYING_FEEDBACK_NONE, 1000, 1000));
}

static void the_volume_bar_fills_in_proportion_to_the_level(void)
{
    CHECK_EQUAL(0, now_playing_volume_width(0, 100));
    CHECK_EQUAL(50, now_playing_volume_width(30, 100));
    CHECK_EQUAL(100, now_playing_volume_width(60, 100));
    CHECK_EQUAL(100, now_playing_volume_width(80, 100));
}

static void progress_fills_in_proportion_to_the_position(void)
{
    CHECK_EQUAL(0, now_playing_progress_width(0, 200000, 300));
    CHECK_EQUAL(150, now_playing_progress_width(100000, 200000, 300));
    CHECK_EQUAL(300, now_playing_progress_width(200000, 200000, 300));
}

static void progress_stays_inside_the_bar(void)
{
    CHECK_EQUAL(300, now_playing_progress_width(250000, 200000, 300));
    CHECK_EQUAL(0, now_playing_progress_width(5000, 0, 300));
}

static void the_codec_has_a_short_name(void)
{
    CHECK(strcmp("FLAC", now_playing_codec_name(HAL_AUDIO_CODEC_FLAC)) == 0);
    CHECK(strcmp("MP3", now_playing_codec_name(HAL_AUDIO_CODEC_MP3)) == 0);
    CHECK(strcmp("WAV", now_playing_codec_name(HAL_AUDIO_CODEC_WAV)) == 0);
    CHECK(strcmp("AAC", now_playing_codec_name(HAL_AUDIO_CODEC_AAC)) == 0);
    CHECK(strcmp("ALAC", now_playing_codec_name(HAL_AUDIO_CODEC_ALAC)) == 0);
    CHECK(strcmp("Vorbis", now_playing_codec_name(HAL_AUDIO_CODEC_VORBIS)) == 0);
    CHECK(strcmp("", now_playing_codec_name(HAL_AUDIO_CODEC_UNKNOWN)) == 0);
}

static void lossless_streams_show_bit_depth_and_rate(void)
{
    CHECK(stream_text_is(HI_RES_FLAC, "24-bit \xC2\xB7 96 kHz"));
}

static void lossy_streams_show_only_the_rate(void)
{
    CHECK(stream_text_is(MP3, "44.1 kHz"));
}

static void an_unknown_stream_shows_nothing(void)
{
    CHECK(stream_text_is((HalAudioFormat){.codec = HAL_AUDIO_CODEC_UNKNOWN}, ""));
}

static void the_detail_line_shows_bitrate_and_place(void)
{
    PlayerStatus status = {.format = HI_RES_FLAC, .place = {3, 10}};
    CHECK(detail_text_is(status, "2,304 kbps \xC2\xB7 3 of 10"));
}

static void the_detail_line_leaves_out_what_is_unknown(void)
{
    CHECK(detail_text_is((PlayerStatus){.place = {3, 10}}, "3 of 10"));
    CHECK(detail_text_is((PlayerStatus){.format = MP3}, "320 kbps"));
    CHECK(detail_text_is((PlayerStatus){0}, ""));
}

static void holding_menu_opens_lyrics_but_does_not_leave_seek_mode(void)
{
    CHECK_EQUAL(NOW_PLAYING_LYRICS, command(KEY_MENU, KEY_GESTURE_LONG_PRESS));
    CHECK_EQUAL(NOW_PLAYING_NONE, scrub_command(KEY_MENU, KEY_GESTURE_LONG_PRESS));
    CHECK_EQUAL(NOW_PLAYING_QUICK_SETTINGS, command(KEY_MENU, KEY_GESTURE_PRESS));
}

static Color playback_pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
static Color previous_pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
static NowPlayingScreen playback_view;
static Theme playback_theme;
static Settings playback_settings;

static void start_passive_view(void)
{
    fake_hal_reset();
    playback_theme = BOARD_THEME;
    playback_settings = settings_defaults();
    now_playing_screen_init(&playback_view, (NowPlayingConfig){.settings = &playback_settings,
                                                               .theme = &playback_theme});
    playback_view.status =
        (PlayerStatus){.has_track = true, .state = PLAYER_PLAYING, .duration_ms = 20000};
}

static void draw_passive_view(uint32_t position_ms, uint32_t now_ms)
{
    playback_view.status.position_ms = position_ms;
    playback_view.now_ms = now_ms;
    Canvas canvas =
        canvas_make(playback_pixels, (Rect){0, 0, HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT});
    playback_view.screen.draw(&playback_view, &canvas);
}

static CoverCache artwork_cache;

static void artwork_has_a_larger_primary_surface_in_every_text_size_and_orientation(void)
{
    Color artwork = COLOR_RGB(240, 160, 40);
    for (size_t index = 0; index < COVER_FULL_PIXELS; index++) {
        artwork_cache.full[index] = artwork;
    }
    artwork_cache.full_state = COVER_LOADED;
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            start_passive_view();
            default_theme_set_text_size(&playback_theme, (TextSize)size);
            default_theme_set_orientation(&playback_theme, (Orientation)orientation);
            playback_view.config.covers = &artwork_cache;
            playback_view.has_text = true;
            Canvas canvas =
                canvas_make(playback_pixels, (Rect){0, 0, playback_theme.metrics.screen_width,
                                                    playback_theme.metrics.screen_height});
            playback_view.screen.draw(&playback_view, &canvas);
            int x = orientation == 0 ? 132 : 35;
            int y = orientation == 0 ? 100 : 140;
            CHECK_EQUAL(artwork, playback_pixels[y * playback_theme.metrics.screen_width + x]);
        }
    }
}

static void portrait_titles_that_fit_two_lines_stay_readable_without_scrolling(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        start_passive_view();
        default_theme_set_text_size(&playback_theme, (TextSize)size);
        default_theme_set_orientation(&playback_theme, ORIENTATION_PORTRAIT);
        strcpy(playback_view.title, "Night Transmission");
        Canvas canvas =
            canvas_make(playback_pixels, (Rect){0, 0, playback_theme.metrics.screen_width,
                                                playback_theme.metrics.screen_height});
        playback_view.now_ms = 1000;
        playback_view.screen.draw(&playback_view, &canvas);
        memcpy(previous_pixels, playback_pixels, sizeof playback_pixels);
        playback_view.now_ms = 1800;
        playback_view.screen.draw(&playback_view, &canvas);
        CHECK(memcmp(previous_pixels, playback_pixels, sizeof playback_pixels) == 0);
    }
}

static void passive_transport_pixels_stay_stable_within_one_displayed_second(void)
{
    start_passive_view();
    draw_passive_view(1000, 1000);
    memcpy(previous_pixels, playback_pixels, sizeof playback_pixels);
    draw_passive_view(1499, 1499);
    CHECK(memcmp(previous_pixels, playback_pixels, sizeof playback_pixels) == 0);
    draw_passive_view(2000, 2000);
    CHECK(memcmp(previous_pixels, playback_pixels, sizeof playback_pixels) != 0);
}

static void a_short_track_cannot_promote_passive_transport_to_continuous_fast_frames(void)
{
    start_passive_view();
    FramePacer pacer = {0};
    int frames = 0;
    for (uint32_t now = 0; now < 10000; now += 4) {
        if (!frame_pacer_due(&pacer, now, false)) {
            continue;
        }
        draw_passive_view(1000 + now, now);
        bool changed =
            frames == 0 || memcmp(previous_pixels, playback_pixels, sizeof playback_pixels) != 0;
        memcpy(previous_pixels, playback_pixels, sizeof playback_pixels);
        frame_pacer_done(&pacer, now, changed, false);
        frames++;
    }
    CHECK(frames >= 10);
    CHECK(frames <= 35);
}

static void scrubbing_keeps_subsecond_cursor_changes_visible(void)
{
    start_passive_view();
    playback_view.scrubbing = true;
    playback_view.scrub_position_ms = 1000;
    draw_passive_view(1000, 1000);
    memcpy(previous_pixels, playback_pixels, sizeof playback_pixels);
    playback_view.scrub_position_ms = 1499;
    draw_passive_view(1000, 1000);
    CHECK(memcmp(previous_pixels, playback_pixels, sizeof playback_pixels) != 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(artwork_has_a_larger_primary_surface_in_every_text_size_and_orientation),
        TEST_CASE(portrait_titles_that_fit_two_lines_stay_readable_without_scrolling),
        TEST_CASE(passive_transport_pixels_stay_stable_within_one_displayed_second),
        TEST_CASE(a_short_track_cannot_promote_passive_transport_to_continuous_fast_frames),
        TEST_CASE(scrubbing_keeps_subsecond_cursor_changes_visible),
        TEST_CASE(holding_menu_opens_lyrics_but_does_not_leave_seek_mode),
        TEST_CASE(the_codec_has_a_short_name),
        TEST_CASE(lossless_streams_show_bit_depth_and_rate),
        TEST_CASE(lossy_streams_show_only_the_rate),
        TEST_CASE(an_unknown_stream_shows_nothing),
        TEST_CASE(the_detail_line_shows_bitrate_and_place),
        TEST_CASE(the_detail_line_leaves_out_what_is_unknown),
        TEST_CASE(a_double_volume_click_skips_with_the_double_volume_setting),
        TEST_CASE(holding_volume_seeks_with_the_double_volume_setting),
        TEST_CASE(holding_volume_while_it_is_shown_changes_it),
        TEST_CASE(play_clicks_skip_only_with_the_play_clicks_setting),
        TEST_CASE(holding_volume_skips_only_with_the_hold_setting),
        TEST_CASE(a_volume_tap_changes_the_volume),
        TEST_CASE(holding_volume_up_skips_forward_and_volume_down_skips_back),
        TEST_CASE(play_toggles_pause_and_a_long_play_starts_seeking),
        TEST_CASE(while_seeking_volume_keys_move_the_target),
        TEST_CASE(while_seeking_play_commits_and_back_cancels),
        TEST_CASE(the_seek_target_stays_inside_the_track),
        TEST_CASE(menu_opens_quick_settings),
        TEST_CASE(back_leaves_and_a_long_back_goes_home),
        TEST_CASE(power_is_left_to_the_system),
        TEST_CASE(volume_changes_show_the_volume_bar),
        TEST_CASE(skips_and_pauses_show_a_badge_for_the_new_state),
        TEST_CASE(the_volume_bar_stays_longer_than_a_badge),
        TEST_CASE(the_volume_bar_fills_in_proportion_to_the_level),
        TEST_CASE(progress_fills_in_proportion_to_the_position),
        TEST_CASE(progress_stays_inside_the_bar),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
