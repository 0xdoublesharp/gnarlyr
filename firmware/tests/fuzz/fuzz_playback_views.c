#include <stddef.h>
#include <stdint.h>

#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "fuzz_source.h"
#include "gfx/canvas.h"
#include "hal/display.h"
#include "player_storage.h"
#include "screens/now_playing_screen.h"
#include "screens/up_next_screen.h"

enum {
    ACTION_LIMIT = 48,
    TEXT_BYTES = 128,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint32_t get32(const uint8_t *bytes, size_t size, size_t offset)
{
    uint32_t value = 0;
    for (size_t index = 0; index < 4 && offset + index < size; index++) {
        value |= (uint32_t)bytes[offset + index] << (index * 8);
    }
    return value;
}

static KeyEvent event_for(uint8_t value)
{
    return (KeyEvent){.key = (Key)(value % KEY_COUNT),
                      .gesture = (KeyGesture)((value / KEY_COUNT) % 5),
                      .repeats = value};
}

static void exercise_helpers(const uint8_t *data, size_t size)
{
    uint8_t first = size > 0 ? data[0] : 0;
    uint8_t second = size > 1 ? data[1] : 0;
    KeyEvent event = event_for(first);
    NowPlayingKeys keys = {.scrubbing = (second & 1u) != 0,
                           .adjusting_volume = (second & 2u) != 0,
                           .skip = (SkipGesture)(second % SKIP_GESTURE_COUNT)};
    NowPlayingCommand command = now_playing_command_for(event, keys);
    fuzz_require(command >= NOW_PLAYING_NONE && command <= NOW_PLAYING_REPEAT_MARK);
    uint32_t position = get32(data, size, 2);
    uint32_t duration = get32(data, size, 6);
    int32_t delta = (int32_t)get32(data, size, 10);
    fuzz_require(now_playing_scrub_target(position, duration, delta) <= duration);
    now_playing_adjusting_volume(position, duration);
    now_playing_progress_width(position, duration, first);
    now_playing_volume_width(first, second);
    NowPlayingFeedback feedback =
        now_playing_feedback_for(command, (PlayerState)(second % (PLAYER_PAUSED + 1)));
    now_playing_feedback_visible(feedback, position, duration);

    HalAudioFormat format = {.codec = (HalAudioCodec)(first % (HAL_AUDIO_CODEC_OPUS + 2)),
                             .sample_rate = get32(data, size, 14),
                             .bits_per_sample = second,
                             .channels = (uint8_t)(second % 4),
                             .bitrate_kbps = get32(data, size, 18)};
    char text[TEXT_BYTES];
    now_playing_codec_name(format.codec);
    now_playing_stream_text(format, text, sizeof text);
    PlayerStatus status = {.state = (PlayerState)(second % (PLAYER_PAUSED + 1)),
                           .has_track = (second & 4u) != 0,
                           .position_ms = position,
                           .duration_ms = duration,
                           .format = format,
                           .place = {(uint16_t)first, (uint16_t)second}};
    now_playing_detail_text(&status, text, sizeof text);
}

static void draw(Screen *screen, const Theme *theme)
{
    static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
    Canvas canvas = canvas_make(
        pixels, (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height});
    screen->draw(screen->context, &canvas);
}

static void check_up_next(UpNextScreen *up_next)
{
    int count = up_next->model.count(up_next->model.context);
    fuzz_require(count >= 0 && count <= UP_NEXT_QUEUE_ROWS + 1);
    if (count > 0) {
        int row = up_next->list.selected;
        fuzz_require(row >= 0 && row < count);
        fuzz_require(up_next->model.label(up_next->model.context, row) != NULL);
        up_next->model.detail(up_next->model.context, row);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    exercise_helpers(data, size);
    fake_hal_reset();
    fake_hal_add_file(PLAYER_SESSION_PATH, NULL, 4096);
    FixtureLibrary fixture;
    fixture_library_open(&fixture);
    Player player;
    player_init(&player, &fixture.library);
    if (size == 0 || (data[0] & 1u) == 0) {
        player_play_list(&player, browse_root_query(BROWSE_ALL_SONGS), 0, 1);
    }
    if (size > 1 && (data[1] & 1u) != 0) {
        player_add_to_up_next(
            &player, (uint16_t)(data[1] % library_count(&fixture.library, LIBRARY_TRACKS)));
    }
    Settings settings = settings_defaults();
    if (size > 2) {
        settings.skip_gesture = (SkipGesture)(data[2] % SKIP_GESTURE_COUNT);
    }
    Theme theme = BOARD_THEME;
    Screen placeholder = {.title = "Destination"};
    Toast toast = {0};
    UpNextScreen up_next;
    Screen *up_next_screen = up_next_screen_init(
        &up_next,
        (UpNextConfig){
            .library = &fixture.library, .theme = &theme, .player = &player, .toast = &toast});
    NowPlayingScreen now_playing;
    Screen *now_playing_screen =
        now_playing_screen_init(&now_playing, (NowPlayingConfig){.player = &player,
                                                                 .settings = &settings,
                                                                 .library = &fixture.library,
                                                                 .theme = &theme,
                                                                 .lyrics = &placeholder});
    up_next_screen->enter(up_next_screen->context, 0);
    now_playing_screen->enter(now_playing_screen->context, 0);
    draw(up_next_screen, &theme);
    draw(now_playing_screen, &theme);

    size_t limit = size < ACTION_LIMIT ? size : ACTION_LIMIT;
    for (size_t index = 0; index < limit; index++) {
        uint8_t value = data[index];
        uint32_t now_ms = (uint32_t)(index + 1) * 257u;
        switch (value >> 5) {
        case 1:
            fake_hal_audio_finish_track();
            break;
        case 2:
            fake_hal_audio_fail_track();
            break;
        case 3:
            player_add_to_up_next(
                &player, (uint16_t)(value % library_count(&fixture.library, LIBRARY_TRACKS)));
            break;
        case 4:
            player_set_repeat(&player, (RepeatMode)(value % REPEAT_MODE_COUNT));
            break;
        case 5:
            player_set_shuffle(&player, (ShuffleMode)(value % SHUFFLE_MODE_COUNT), value);
            break;
        case 6:
            player_toggle_pause(&player);
            break;
        default:
            break;
        }
        KeyEvent event = event_for(value);
        ScreenAction now_action =
            now_playing_screen->handle_key(now_playing_screen->context, event, now_ms);
        ScreenAction next_action =
            up_next_screen->handle_key(up_next_screen->context, event, now_ms);
        fuzz_require(now_action.type <= SCREEN_ACTION_REPLACE);
        fuzz_require(next_action.type <= SCREEN_ACTION_REPLACE);
        if (next_action.target != NULL && next_action.target != up_next_screen &&
            next_action.target->enter != NULL) {
            next_action.target->enter(next_action.target->context, now_ms);
            if (next_action.target->handle_key != NULL) {
                next_action.target->handle_key(next_action.target->context, event, now_ms);
            }
        }
        player_update(&player, now_ms);
        now_playing_screen->update(now_playing_screen->context, now_ms);
        up_next_screen->update(up_next_screen->context, now_ms);
        check_up_next(&up_next);
        if ((value & 7u) == 0) {
            draw(now_playing_screen, &theme);
            draw(up_next_screen, &theme);
        }
    }
    player_release_output(&player);
    return 0;
}
