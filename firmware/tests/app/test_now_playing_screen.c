#include <string.h>

#include "board_layout.h"
#include "check.h"
#include "core/frame_pacer.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "gfx/strip_renderer.h"
#include "gfx/text.h"
#include "hal/display.h"
#include "screen_snapshot.h"
#include "screens/now_playing_screen.h"
#include "ui/marquee.h"

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

static bool format_text_is(PlayerStatus status, unsigned parts, const char *expected)
{
    char buffer[TEXT_CAPACITY];
    now_playing_format_text(&status, buffer, sizeof buffer, parts);
    return strcmp(expected, buffer) == 0;
}

static NowPlayingCommand command(Key key, KeyGesture gesture)
{
    return now_playing_command_for((KeyEvent){.key = key, .gesture = gesture});
}

static NowPlayingCommand chord_command(Key key, KeyGesture gesture, uint8_t repeats)
{
    return now_playing_command_for(
        (KeyEvent){.key = key, .gesture = gesture, .repeats = repeats, .chord = KEY_CHORD_PLAY});
}

static NowPlayingCommand menu_chord_command(Key key, KeyGesture gesture, uint8_t repeats)
{
    return now_playing_command_for(
        (KeyEvent){.key = key, .gesture = gesture, .repeats = repeats, .chord = KEY_CHORD_MENU});
}

static void volume_keys_alone_only_change_the_volume(void)
{
    static const KeyGesture GESTURES[] = {KEY_GESTURE_PRESS, KEY_GESTURE_LONG_PRESS,
                                          KEY_GESTURE_REPEAT};
    for (size_t index = 0; index < sizeof GESTURES / sizeof GESTURES[0]; index++) {
        CHECK_EQUAL(NOW_PLAYING_VOLUME_UP, command(KEY_VOLUME_UP, GESTURES[index]));
        CHECK_EQUAL(NOW_PLAYING_VOLUME_DOWN, command(KEY_VOLUME_DOWN, GESTURES[index]));
    }
}

static void holding_play_and_tapping_volume_skips(void)
{
    CHECK_EQUAL(NOW_PLAYING_NEXT, chord_command(KEY_VOLUME_UP, KEY_GESTURE_PRESS, 0));
    CHECK_EQUAL(NOW_PLAYING_PREVIOUS, chord_command(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS, 0));
}

static void holding_play_and_holding_volume_skips_once_without_repeating(void)
{
    CHECK_EQUAL(NOW_PLAYING_NEXT, chord_command(KEY_VOLUME_UP, KEY_GESTURE_LONG_PRESS, 0));
    CHECK_EQUAL(NOW_PLAYING_PREVIOUS, chord_command(KEY_VOLUME_DOWN, KEY_GESTURE_LONG_PRESS, 0));
    CHECK_EQUAL(NOW_PLAYING_NONE, chord_command(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, 1));
    CHECK_EQUAL(NOW_PLAYING_NONE, chord_command(KEY_VOLUME_DOWN, KEY_GESTURE_REPEAT, 40));
}

static void holding_menu_and_tapping_volume_moves_ten_seconds(void)
{
    CHECK_EQUAL(NOW_PLAYING_SEEK_FORWARD, menu_chord_command(KEY_VOLUME_UP, KEY_GESTURE_PRESS, 0));
    CHECK_EQUAL(NOW_PLAYING_SEEK_BACK, menu_chord_command(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS, 0));
}

static void holding_menu_and_holding_volume_starts_and_keeps_a_preview(void)
{
    CHECK_EQUAL(NOW_PLAYING_SEEK_FORWARD_START,
                menu_chord_command(KEY_VOLUME_UP, KEY_GESTURE_LONG_PRESS, 0));
    CHECK_EQUAL(NOW_PLAYING_SEEK_BACK_START,
                menu_chord_command(KEY_VOLUME_DOWN, KEY_GESTURE_LONG_PRESS, 0));
    CHECK_EQUAL(NOW_PLAYING_SEEK_FORWARD_HELD,
                menu_chord_command(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, 1));
    CHECK_EQUAL(NOW_PLAYING_SEEK_FORWARD_HELD,
                menu_chord_command(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, 200));
    CHECK_EQUAL(NOW_PLAYING_SEEK_BACK_HELD,
                menu_chord_command(KEY_VOLUME_DOWN, KEY_GESTURE_REPEAT, 2));
}

static void a_gesture_outside_the_known_ones_runs_no_command(void)
{
    for (int gesture = KEY_GESTURE_REPEAT + 1; gesture <= UINT8_MAX; gesture++) {
        for (int key = 0; key < KEY_COUNT; key++) {
            KeyGesture odd = (KeyGesture)gesture;
            CHECK_EQUAL(NOW_PLAYING_NONE, command((Key)key, odd));
            CHECK_EQUAL(NOW_PLAYING_NONE, chord_command((Key)key, odd, 0));
            CHECK_EQUAL(NOW_PLAYING_NONE, menu_chord_command((Key)key, odd, 0));
        }
    }
}

static void the_skip_toasts_name_the_direction(void)
{
    CHECK(strcmp("Next track", i18n_text(STR_SKIPPED_TO_NEXT)) == 0);
    CHECK(strcmp("Previous track", i18n_text(STR_SKIPPED_TO_PREVIOUS)) == 0);
}

static void skip_toasts_say_what_the_skip_did(void)
{
    PlayerStatus before = {.has_track = true, .track = 4, .position_ms = 40000};
    before.place.number = 2;
    PlayerStatus moved = before;
    moved.track = 5;
    moved.place.number = 3;
    moved.position_ms = 0;
    PlayerStatus restarted = before;
    restarted.position_ms = 0;
    CHECK_EQUAL(STR_SKIPPED_TO_NEXT, now_playing_skip_outcome(true, before, moved));
    CHECK_EQUAL(STR_SKIPPED_TO_PREVIOUS, now_playing_skip_outcome(false, before, moved));
    CHECK_EQUAL(STR_SKIPPED_TO_START, now_playing_skip_outcome(false, before, restarted));
    CHECK_EQUAL(STR_SKIPPED_TO_NEXT, now_playing_skip_outcome(true, before, restarted));
    CHECK_EQUAL(STR_SKIPPED_NOTHING, now_playing_skip_outcome(true, before, before));
    PlayerStatus at_start = before;
    at_start.position_ms = 0;
    CHECK_EQUAL(STRING_COUNT, now_playing_skip_outcome(false, at_start, at_start));
    before.spoken = true;
    CHECK_EQUAL(STR_JUMPED_FORWARD, now_playing_skip_outcome(true, before, before));
    CHECK_EQUAL(STR_JUMPED_BACK, now_playing_skip_outcome(false, before, before));
}

static void play_toggles_pause_and_a_long_play_does_nothing_more(void)
{
    CHECK_EQUAL(NOW_PLAYING_TOGGLE_PAUSE, command(KEY_PLAY, KEY_GESTURE_PRESS));
    CHECK_EQUAL(NOW_PLAYING_NONE, command(KEY_PLAY, KEY_GESTURE_LONG_PRESS));
}

static void the_seek_target_stays_inside_the_track(void)
{
    CHECK_EQUAL(65000, now_playing_scrub_target(60000, 200000, 5000));
    CHECK_EQUAL(0, now_playing_scrub_target(3000, 200000, -5000));
    CHECK_EQUAL(199000, now_playing_scrub_target(198000, 200000, 15000));
    CHECK_EQUAL(199000, now_playing_scrub_target(200000, 200000, -1000));
    CHECK_EQUAL(199500, now_playing_scrub_target(199500, 200000, 0));
    CHECK_EQUAL(600, now_playing_scrub_target(600, 800, 100));
    CHECK_EQUAL(500, now_playing_scrub_target(600, 800, -100));
    CHECK_EQUAL(1780, now_playing_scrub_target(1800, 2000, -20));
}

static void the_seek_target_of_an_unknown_length_stays_put(void)
{
    CHECK_EQUAL(3000, now_playing_scrub_target(3000, 0, 2000));
    CHECK_EQUAL(3000, now_playing_scrub_target(3000, 0, -6000));
}

static void a_tap_moves_ten_seconds(void)
{
    CHECK_EQUAL(10000, NOW_PLAYING_SEEK_TAP_MS);
    CHECK_EQUAL(70000, now_playing_scrub_target(60000, 200000, NOW_PLAYING_SEEK_TAP_MS));
    CHECK_EQUAL(50000, now_playing_scrub_target(60000, 200000, -NOW_PLAYING_SEEK_TAP_MS));
    CHECK_EQUAL(0, now_playing_scrub_target(4000, 200000, -NOW_PLAYING_SEEK_TAP_MS));
    CHECK_EQUAL(199000, now_playing_scrub_target(195000, 200000, NOW_PLAYING_SEEK_TAP_MS));
    CHECK_EQUAL(199500, now_playing_scrub_target(199500, 200000, NOW_PLAYING_SEEK_TAP_MS));
}

static void a_held_scrub_crosses_a_three_minute_track_in_ten_seconds_then_three(void)
{
    CHECK_EQUAL(18000, now_playing_scrub_rate(180000, 0));
    CHECK_EQUAL(60000, now_playing_scrub_rate(180000, NOW_PLAYING_SCRUB_FAST_AFTER_MS));
}

static void a_held_scrub_of_an_hour_moves_one_minute_then_ten_minutes_a_second(void)
{
    CHECK_EQUAL(60000, now_playing_scrub_rate(3600000, 0));
    CHECK_EQUAL(600000, now_playing_scrub_rate(3600000, NOW_PLAYING_SCRUB_FAST_AFTER_MS));
}

static void a_held_scrub_of_ten_hours_keeps_the_same_caps(void)
{
    CHECK_EQUAL(60000, now_playing_scrub_rate(36000000, 0));
    CHECK_EQUAL(600000, now_playing_scrub_rate(36000000, 5000));
    CHECK_EQUAL(60000, now_playing_scrub_rate(UINT32_MAX, 0));
    CHECK_EQUAL(600000, now_playing_scrub_rate(UINT32_MAX, UINT32_MAX));
}

static void a_held_scrub_speeds_up_after_about_two_seconds(void)
{
    CHECK_EQUAL(1500, NOW_PLAYING_SCRUB_FAST_AFTER_MS);
    CHECK_EQUAL(18000, now_playing_scrub_rate(180000, NOW_PLAYING_SCRUB_FAST_AFTER_MS - 1));
    CHECK_EQUAL(60000, now_playing_scrub_rate(180000, NOW_PLAYING_SCRUB_FAST_AFTER_MS));
}

static void the_scrub_caps_are_reached_at_ten_and_thirty_minutes(void)
{
    CHECK_EQUAL(59999, now_playing_scrub_rate(599999, 0));
    CHECK_EQUAL(60000, now_playing_scrub_rate(600000, 0));
    CHECK_EQUAL(599999, now_playing_scrub_rate(1799997, NOW_PLAYING_SCRUB_FAST_AFTER_MS));
    CHECK_EQUAL(600000, now_playing_scrub_rate(1800000, NOW_PLAYING_SCRUB_FAST_AFTER_MS));
}

static void the_preview_distance_is_the_rate_times_the_time_held(void)
{
    CHECK_EQUAL(0, now_playing_scrub_distance(180000, 0));
    CHECK_EQUAL(18000, now_playing_scrub_distance(180000, 1000));
    CHECK_EQUAL(27000, now_playing_scrub_distance(180000, NOW_PLAYING_SCRUB_FAST_AFTER_MS));
    CHECK_EQUAL(27000 + 60000, now_playing_scrub_distance(180000, 2500));
    CHECK_EQUAL(90000 + 600000, now_playing_scrub_distance(3600000, 2500));
}

static void the_preview_distance_never_passes_the_length(void)
{
    CHECK_EQUAL(180000, now_playing_scrub_distance(180000, 10000));
    CHECK_EQUAL(36000000, now_playing_scrub_distance(36000000, UINT32_MAX));
    CHECK_EQUAL(UINT32_MAX, now_playing_scrub_distance(UINT32_MAX, UINT32_MAX));
    CHECK_EQUAL(0, now_playing_scrub_distance(0, 5000));
}

static NowPlayingScrub preview_from(uint32_t base_ms, bool back)
{
    return (NowPlayingScrub){.started_ms = 5000,
                             .base_ms = base_ms,
                             .duration_ms = 180000,
                             .active = true,
                             .back = back};
}

static void the_preview_glides_from_where_the_hold_began(void)
{
    NowPlayingScrub forward = preview_from(60000, false);
    NowPlayingScrub back = preview_from(60000, true);
    CHECK_EQUAL(60000, now_playing_scrub_preview(&forward, 5000));
    CHECK_EQUAL(61800, now_playing_scrub_preview(&forward, 5100));
    CHECK_EQUAL(78000, now_playing_scrub_preview(&forward, 6000));
    CHECK_EQUAL(42000, now_playing_scrub_preview(&back, 6000));
    CHECK_EQUAL(147000, now_playing_scrub_preview(&forward, 7500));
}

static void the_preview_stays_inside_the_track(void)
{
    NowPlayingScrub forward = preview_from(170000, false);
    NowPlayingScrub back = preview_from(10000, true);
    CHECK_EQUAL(179000, now_playing_scrub_preview(&forward, 6000));
    CHECK_EQUAL(0, now_playing_scrub_preview(&back, 6000));
    NowPlayingScrub past_end = preview_from(190000, true);
    CHECK_EQUAL(172000, now_playing_scrub_preview(&past_end, 6000));
    NowPlayingScrub at_end = preview_from(179500, false);
    CHECK_EQUAL(179500, now_playing_scrub_preview(&at_end, 6000));
    at_end.back = true;
    CHECK_EQUAL(177700, now_playing_scrub_preview(&at_end, 5100));
}

static void a_backward_preview_leaves_the_last_second_at_the_normal_rate(void)
{
    NowPlayingScrub chapter = {
        .started_ms = 5000, .base_ms = 1800, .duration_ms = 2000, .active = true, .back = true};
    CHECK_EQUAL(1780, now_playing_scrub_preview(&chapter, 5100));
    NowPlayingScrub part = {
        .started_ms = 5000, .base_ms = 600, .duration_ms = 800, .active = true, .back = true};
    CHECK_EQUAL(592, now_playing_scrub_preview(&part, 5100));
}

static void a_release_stamped_before_the_hold_began_stays_at_the_start(void)
{
    NowPlayingScrub forward = preview_from(60000, false);
    CHECK_EQUAL(60000, now_playing_scrub_preview(&forward, 4990));
    CHECK_EQUAL(60000, now_playing_scrub_preview(&forward, 0));
}

static void a_preview_released_where_it_began_moves_nothing(void)
{
    NowPlayingScreen screen = {.scrub = preview_from(60000, false)};
    screen.scrub.shown_ms = 60000;
    now_playing_scrub_commit(&screen);
    CHECK(!screen.scrub.active);
    CHECK_EQUAL(0, fake_hal_audio_seek_count());
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
                now_playing_feedback_for(NOW_PLAYING_SEEK_FORWARD, PLAYER_PLAYING));
}

static void the_volume_bar_stays_longer_than_a_badge(void)
{
    uint32_t end = 1000 + NOW_PLAYING_VOLUME_FEEDBACK_MS;
    CHECK(now_playing_feedback_visible(NOW_PLAYING_FEEDBACK_VOLUME, 1000, end - 1));
    CHECK(!now_playing_feedback_visible(NOW_PLAYING_FEEDBACK_VOLUME, 1000, end));
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

static void the_format_line_shows_codec_depth_rate_and_bitrate(void)
{
    PlayerStatus status = {.format = HI_RES_FLAC, .place = {3, 10}};
    CHECK(format_text_is(status, NOW_PLAYING_FORMAT_ALL,
                         "FLAC \xC2\xB7 24-bit \xC2\xB7 96 kHz \xC2\xB7 2,304 kbps"));
}

static void the_format_line_splits_into_stream_and_bitrate(void)
{
    PlayerStatus status = {.format = HI_RES_FLAC};
    CHECK(
        format_text_is(status, NOW_PLAYING_FORMAT_STREAM, "FLAC \xC2\xB7 24-bit \xC2\xB7 96 kHz"));
    CHECK(format_text_is(status, NOW_PLAYING_FORMAT_BITRATE, "2,304 kbps"));
}

static void the_format_line_leaves_out_what_is_unknown(void)
{
    CHECK(format_text_is((PlayerStatus){.place = {3, 10}}, NOW_PLAYING_FORMAT_ALL, ""));
    CHECK(format_text_is((PlayerStatus){.format = MP3}, NOW_PLAYING_FORMAT_ALL,
                         "MP3 \xC2\xB7 44.1 kHz \xC2\xB7 320 kbps"));
    CHECK(format_text_is((PlayerStatus){0}, NOW_PLAYING_FORMAT_ALL, ""));
}

static void menu_alone_opens_quick_settings_or_lyrics(void)
{
    CHECK_EQUAL(NOW_PLAYING_LYRICS, command(KEY_MENU, KEY_GESTURE_LONG_PRESS));
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

static void a_paused_resume_shows_its_planned_start_in_every_layout(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            start_passive_view();
            default_theme_set_text_size(&playback_theme, (TextSize)size);
            default_theme_set_orientation(&playback_theme, (Orientation)orientation);
            Player paused = {.state = PLAYER_PAUSED,
                             .paused_position_ms = 42000,
                             .rewind_pause = RESUME_REWIND_COLD,
                             .rewind_on_resume = true};
            playback_view.status = player_status(&paused);
            CHECK_EQUAL(22000, playback_view.status.position_ms);
            playback_view.status.has_track = true;
            playback_view.status.duration_ms = 60000;
            playback_view.has_text = true;
            strcpy(playback_view.title, "Resume preview");
            Canvas canvas =
                canvas_make(playback_pixels, (Rect){0, 0, playback_theme.metrics.screen_width,
                                                    playback_theme.metrics.screen_height});
            canvas_fill_rect(&canvas, canvas.bounds, playback_theme.colors.background);
            playback_view.screen.draw(&playback_view, &canvas);
            screen_snapshot_write(&canvas, "resume-preview", orientation * 3 + size);
        }
    }
}

static Color full_frame[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
static Color band_frame[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
static Color band_strip[HAL_DISPLAY_WIDTH * 23];
static uint32_t frame_position_ms;
static uint32_t frame_now_ms;

static bool patterned_art(const Theme *theme, Canvas *canvas, int slot, Rect area)
{
    (void)theme;
    (void)slot;
    Rect clip = rect_intersection(canvas->clip, area);
    for (int y = clip.y; y < clip.y + clip.height; y++) {
        for (int x = clip.x; x < clip.x + clip.width; x++) {
            canvas->pixels[(y - canvas->bounds.y) * canvas->bounds.width + x - canvas->bounds.x] =
                (Color)(x * 31 + y * 977);
        }
    }
    return true;
}

static void draw_frame_view(void *context, Canvas *canvas)
{
    (void)context;
    playback_view.status.position_ms = frame_position_ms;
    playback_view.now_ms = frame_now_ms;
    playback_view.screen.draw(&playback_view, canvas);
}

static void copy_rows(void *context, Rect area, const Color *pixels)
{
    Color *frame = context;
    memcpy(frame + area.y * area.width, pixels, (size_t)(area.width * area.height) * sizeof(Color));
}

static void render_rows(Color *frame, Rect area)
{
    StripRenderer renderer = {band_strip, area,
                              (int)(sizeof band_strip / sizeof band_strip[0]) / area.width,
                              playback_theme.colors.background, false};
    strip_renderer_render(&renderer, (FrameDrawer){draw_frame_view, NULL},
                          (FrameSink){copy_rows, frame});
}

typedef struct BandCase {
    uint32_t first_ms;
    uint32_t next_ms;
    uint32_t first_position_ms;
    uint32_t next_position_ms;
    uint8_t bands;
    bool preview;
} BandCase;

static Library preview_library;
static Player preview_player;

static void preview_scrub(void)
{
    preview_player =
        (Player){.library = &preview_library, .has_track = true, .state = PLAYER_PAUSED};
    playback_view.config.player = &preview_player;
    playback_view.status.duration_ms = 200000;
    playback_view.scrub = (NowPlayingScrub){.started_ms = 1000,
                                            .base_ms = 30000,
                                            .duration_ms = 200000,
                                            .token = player_token(&preview_player),
                                            .active = true};
}

static void check_band_case(BandCase band, bool art)
{
    Rect screen = {0, 0, playback_theme.metrics.screen_width, playback_theme.metrics.screen_height};
    playback_theme.draw_art = art ? patterned_art : NULL;
    if (band.preview) {
        preview_scrub();
    }
    frame_now_ms = band.first_ms;
    frame_position_ms = band.first_position_ms;
    marquee_watch_start();
    render_rows(band_frame, screen);
    Rect rows[MARQUEE_BAND_COUNT];
    for (int at = 0; at < MARQUEE_BAND_COUNT; at++) {
        rows[at] = marquee_watch_band((MarqueeBand)at);
    }
    frame_now_ms = band.next_ms;
    frame_position_ms = band.next_position_ms;
    render_rows(full_frame, screen);
    marquee_watch_start();
    for (int at = 0; at < MARQUEE_BAND_COUNT; at++) {
        if ((band.bands & 1u << at) && rows[at].height > 0) {
            render_rows(band_frame, (Rect){0, rows[at].y, screen.width, rows[at].height});
        }
    }
    size_t bytes = (size_t)(screen.width * screen.height) * sizeof(Color);
    CHECK(memcmp(full_frame, band_frame, bytes) == 0);
}

static void a_band_frame_leaves_exactly_the_pixels_of_a_full_frame(void)
{
    static const BandCase CASES[] = {
        {1300, 1400, 5000, 5400, 1u << MARQUEE_BAND_TEXT, false},
        {300, 400, 2000, 17000, 1u << MARQUEE_BAND_TIME | 1u << MARQUEE_BAND_TAPE, false},
        {1300, 1400, 5900, 6100,
         1u << MARQUEE_BAND_TEXT | 1u << MARQUEE_BAND_TIME | 1u << MARQUEE_BAND_TAPE, false},
        {1300, 1400, 5000, 5000, 1u << MARQUEE_BAND_TEXT, true},
    };
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            for (unsigned at = 0; at < sizeof CASES / sizeof CASES[0]; at++) {
                for (int art = 0; art < 2; art++) {
                    start_passive_view();
                    default_theme_set_text_size(&playback_theme, (TextSize)size);
                    default_theme_set_orientation(&playback_theme, (Orientation)orientation);
                    playback_view.has_text = true;
                    strcpy(playback_view.title,
                           "A title far too long for any line of this screen at any text size");
                    check_band_case(CASES[at], art != 0);
                }
            }
        }
    }
}

static Library unknown_length_library;
static Player unknown_length_player;

static void start_unknown_length_view(void)
{
    start_passive_view();
    unknown_length_player = (Player){.library = &unknown_length_library,
                                     .has_track = true,
                                     .state = PLAYER_PAUSED,
                                     .paused_position_ms = 42000};
    playback_view.config.player = &unknown_length_player;
    playback_view.config.library = &unknown_length_library;
    playback_view.screen.enter(&playback_view, 0);
}

static void press_on_view(Key key, KeyGesture gesture, uint8_t repeats, uint8_t chord)
{
    KeyEvent event = {.key = key, .gesture = gesture, .repeats = repeats, .chord = chord};
    playback_view.screen.handle_key(&playback_view, event, 0);
}

static void scrubbing_a_track_of_unknown_length_leaves_its_position(void)
{
    start_unknown_length_view();
    CHECK_EQUAL(0, playback_view.status.duration_ms);
    press_on_view(KEY_VOLUME_UP, KEY_GESTURE_PRESS, 0, KEY_CHORD_MENU);
    press_on_view(KEY_VOLUME_UP, KEY_GESTURE_LONG_PRESS, 0, KEY_CHORD_MENU);
    CHECK(!playback_view.scrub.active);
    press_on_view(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, 20, KEY_CHORD_MENU);
    press_on_view(KEY_VOLUME_DOWN, KEY_GESTURE_REPEAT, 20, KEY_CHORD_MENU);
    CHECK_EQUAL(42000, player_status(&unknown_length_player).position_ms);
    unknown_length_player.loaded = true;
    press_on_view(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, 20, KEY_CHORD_MENU);
    press_on_view(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS, 0, KEY_CHORD_MENU);
    CHECK_EQUAL(0, fake_hal_audio_seek_count());
}

static Player preview_player;

static void start_preview_view(int orientation, int size)
{
    start_passive_view();
    default_theme_set_text_size(&playback_theme, (TextSize)size);
    default_theme_set_orientation(&playback_theme, (Orientation)orientation);
    preview_player = (Player){.has_track = true, .current_token = 7};
    playback_view.config.player = &preview_player;
    playback_view.has_text = true;
    strcpy(playback_view.title, "Scrub preview");
    playback_view.scrub = (NowPlayingScrub){
        .started_ms = 5000, .base_ms = 2000, .duration_ms = 20000, .token = 7, .active = true};
}

static uint32_t rows_hash(int top, int height)
{
    uint32_t hash = 2166136261u;
    int width = playback_theme.metrics.screen_width;
    for (int y = top; y < top + height; y++) {
        for (int x = 0; x < width; x++) {
            hash = (hash ^ playback_pixels[y * width + x]) * 16777619u;
        }
    }
    return hash;
}

typedef struct DrawnRows {
    uint32_t bar;
    uint32_t times;
    uint32_t icon;
} DrawnRows;

static DrawnRows rows_drawn_at(uint32_t position_ms, uint32_t now_ms)
{
    Canvas canvas = canvas_make(playback_pixels, (Rect){0, 0, playback_theme.metrics.screen_width,
                                                        playback_theme.metrics.screen_height});
    canvas_fill_rect(&canvas, canvas.bounds, playback_theme.colors.background);
    draw_passive_view(position_ms, now_ms);
    NowPlayingFrame frame = now_playing_frame(&playback_theme, false);
    int line = frame.times.height;
    int padding = playback_theme.metrics.padding;
    uint32_t icon = 2166136261u;
    for (int y = frame.times.y; y < frame.times.y + line; y++) {
        for (int x = padding; x < padding + 12; x++) {
            icon =
                (icon ^ playback_pixels[y * playback_theme.metrics.screen_width + x]) * 16777619u;
        }
    }
    return (DrawnRows){rows_hash(frame.progress.y - 3, frame.progress.height + 6),
                       rows_hash(frame.times.y, line), icon};
}

static bool rows_equal(DrawnRows left, DrawnRows right)
{
    return left.bar == right.bar && left.times == right.times && left.icon == right.icon;
}

static DrawnRows preview_drawn(uint32_t base_ms, uint32_t real_ms, uint32_t now_ms)
{
    playback_view.scrub.base_ms = base_ms;
    return rows_drawn_at(real_ms, now_ms);
}

static void the_progress_bar_and_time_follow_the_preview_target(void)
{
    start_preview_view(0, TEXT_SIZE_MEDIUM);
    DrawnRows moved = preview_drawn(2000, 2000, 6000);
    CHECK(rows_equal(preview_drawn(4000, 9000, 5000), moved));
    CHECK(!rows_equal(preview_drawn(2000, 2000, 5000), moved));
}

static void the_preview_is_labelled_and_keeps_the_play_state(void)
{
    start_preview_view(0, TEXT_SIZE_MEDIUM);
    DrawnRows preview = preview_drawn(4000, 4000, 5000);
    playback_view.scrub.active = false;
    DrawnRows plain = rows_drawn_at(4000, 5000);
    CHECK(preview.bar != plain.bar);
    CHECK(preview.times != plain.times);
    CHECK_EQUAL(plain.icon, preview.icon);
}

static void a_preview_from_another_track_shows_the_real_position(void)
{
    start_preview_view(0, TEXT_SIZE_MEDIUM);
    preview_player.current_token = 8;
    DrawnRows stale = rows_drawn_at(2000, 6000);
    playback_view.scrub.active = false;
    CHECK(rows_equal(rows_drawn_at(2000, 6000), stale));
}

static void the_preview_wins_over_the_volume_bar(void)
{
    start_preview_view(0, TEXT_SIZE_MEDIUM);
    DrawnRows preview = rows_drawn_at(2000, 6000);
    playback_view.feedback = NOW_PLAYING_FEEDBACK_VOLUME;
    playback_view.feedback_started_ms = 5900;
    CHECK(rows_equal(rows_drawn_at(2000, 6000), preview));
}

static void the_preview_is_drawn_in_every_text_size_and_orientation(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            start_preview_view(orientation, size);
            Canvas canvas =
                canvas_make(playback_pixels, (Rect){0, 0, playback_theme.metrics.screen_width,
                                                    playback_theme.metrics.screen_height});
            DrawnRows moved = preview_drawn(2000, 2000, 6000);
            screen_snapshot_write(&canvas, "scrub-preview", orientation * 3 + size);
            CHECK(rows_equal(preview_drawn(4000, 9000, 5000), moved));
        }
    }
}

static Toast scrub_toast;

static void scrubbing_an_unknown_length_says_so(void)
{
    start_unknown_length_view();
    scrub_toast = (Toast){0};
    playback_view.config.toast = &scrub_toast;
    press_on_view(KEY_VOLUME_UP, KEY_GESTURE_LONG_PRESS, 0, KEY_CHORD_MENU);
    CHECK(!playback_view.scrub.active);
    CHECK(scrub_toast.visible && strcmp("Length unknown", scrub_toast.text) == 0);
    scrub_toast = (Toast){0};
    press_on_view(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS, 0, KEY_CHORD_MENU);
    CHECK(scrub_toast.visible && strcmp("Length unknown", scrub_toast.text) == 0);
    CHECK_EQUAL(0, fake_hal_audio_seek_count());
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
            Rect art = now_playing_frame(&playback_theme, false).art;
            CHECK(art.width >= COVER_FULL_SIZE);
            int x = art.x + art.width - 4;
            int y = art.y + art.height / 2;
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

static void now_playing_keeps_the_status_band_on_its_header_palette(void)
{
    start_passive_view();
    playback_theme.colors.header_background = COLOR_RGB(16, 32, 48);
    playback_theme.colors.background = COLOR_RGB(248, 240, 224);
    draw_passive_view(0, 1000);
    CHECK_EQUAL(playback_theme.colors.header_background, playback_pixels[0]);
}

static void classic_status_icons_keep_their_header_background(void)
{
    start_passive_view();
    default_theme_set_appearance(&playback_theme, APPEARANCE_CLASSIC);
    draw_passive_view(1000, 1000);
    CHECK_EQUAL(playback_theme.colors.header_background, playback_pixels[0]);
}

static void ab_hints_fit_every_text_size_and_orientation(void)
{
    static const StringId hints[] = {STR_AB_OFF, STR_AB_A, STR_AB_LOOP};
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            for (int state = 0; state < 3; state++) {
                start_passive_view();
                default_theme_set_text_size(&playback_theme, (TextSize)size);
                default_theme_set_orientation(&playback_theme, (Orientation)orientation);
                playback_view.status.ab_repeat = (uint8_t)state;
                playback_view.repeat_feedback_ms = 1000;
                playback_view.now_ms = 1000;
                strcpy(playback_view.title, "Night Transmission");
                strcpy(playback_view.artist, "The Reviewers");
                Canvas canvas =
                    canvas_make(playback_pixels, (Rect){0, 0, playback_theme.metrics.screen_width,
                                                        playback_theme.metrics.screen_height});
                CHECK(text_width(playback_theme.fonts.small, i18n_text(hints[state])) <=
                      HAL_DISPLAY_SHORT_SIDE - 2 * playback_theme.metrics.padding);
                playback_view.screen.draw(&playback_view, &canvas);
                screen_snapshot_write(&canvas, "ab-repeat",
                                      (orientation * TEXT_SIZE_COUNT + size) * 3 + state);
            }
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_band_frame_leaves_exactly_the_pixels_of_a_full_frame),
        TEST_CASE(a_paused_resume_shows_its_planned_start_in_every_layout),
        TEST_CASE(now_playing_keeps_the_status_band_on_its_header_palette),
        TEST_CASE(classic_status_icons_keep_their_header_background),
        TEST_CASE(ab_hints_fit_every_text_size_and_orientation),
        TEST_CASE(artwork_has_a_larger_primary_surface_in_every_text_size_and_orientation),
        TEST_CASE(portrait_titles_that_fit_two_lines_stay_readable_without_scrolling),
        TEST_CASE(passive_transport_pixels_stay_stable_within_one_displayed_second),
        TEST_CASE(a_short_track_cannot_promote_passive_transport_to_continuous_fast_frames),
        TEST_CASE(menu_alone_opens_quick_settings_or_lyrics),
        TEST_CASE(the_codec_has_a_short_name),
        TEST_CASE(lossless_streams_show_bit_depth_and_rate),
        TEST_CASE(lossy_streams_show_only_the_rate),
        TEST_CASE(an_unknown_stream_shows_nothing),
        TEST_CASE(the_format_line_shows_codec_depth_rate_and_bitrate),
        TEST_CASE(the_format_line_splits_into_stream_and_bitrate),
        TEST_CASE(the_format_line_leaves_out_what_is_unknown),
        TEST_CASE(volume_keys_alone_only_change_the_volume),
        TEST_CASE(holding_play_and_tapping_volume_skips),
        TEST_CASE(holding_play_and_holding_volume_skips_once_without_repeating),
        TEST_CASE(holding_menu_and_tapping_volume_moves_ten_seconds),
        TEST_CASE(a_gesture_outside_the_known_ones_runs_no_command),
        TEST_CASE(holding_menu_and_holding_volume_starts_and_keeps_a_preview),
        TEST_CASE(the_skip_toasts_name_the_direction),
        TEST_CASE(skip_toasts_say_what_the_skip_did),
        TEST_CASE(play_toggles_pause_and_a_long_play_does_nothing_more),
        TEST_CASE(the_seek_target_stays_inside_the_track),
        TEST_CASE(the_seek_target_of_an_unknown_length_stays_put),
        TEST_CASE(scrubbing_a_track_of_unknown_length_leaves_its_position),
        TEST_CASE(the_progress_bar_and_time_follow_the_preview_target),
        TEST_CASE(the_preview_is_labelled_and_keeps_the_play_state),
        TEST_CASE(a_preview_from_another_track_shows_the_real_position),
        TEST_CASE(scrubbing_an_unknown_length_says_so),
        TEST_CASE(the_preview_wins_over_the_volume_bar),
        TEST_CASE(the_preview_is_drawn_in_every_text_size_and_orientation),
        TEST_CASE(a_tap_moves_ten_seconds),
        TEST_CASE(a_held_scrub_crosses_a_three_minute_track_in_ten_seconds_then_three),
        TEST_CASE(a_held_scrub_of_an_hour_moves_one_minute_then_ten_minutes_a_second),
        TEST_CASE(a_held_scrub_of_ten_hours_keeps_the_same_caps),
        TEST_CASE(a_held_scrub_speeds_up_after_about_two_seconds),
        TEST_CASE(the_scrub_caps_are_reached_at_ten_and_thirty_minutes),
        TEST_CASE(the_preview_distance_is_the_rate_times_the_time_held),
        TEST_CASE(the_preview_distance_never_passes_the_length),
        TEST_CASE(the_preview_glides_from_where_the_hold_began),
        TEST_CASE(the_preview_stays_inside_the_track),
        TEST_CASE(a_release_stamped_before_the_hold_began_stays_at_the_start),
        TEST_CASE(a_backward_preview_leaves_the_last_second_at_the_normal_rate),
        TEST_CASE(a_preview_released_where_it_began_moves_nothing),
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
