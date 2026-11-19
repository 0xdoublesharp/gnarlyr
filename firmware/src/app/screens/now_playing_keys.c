#include "now_playing_screen.h"

static NowPlayingCommand by_gesture(KeyGesture gesture, NowPlayingCommand press,
                                    NowPlayingCommand hold)
{
    switch (gesture) {
    case KEY_GESTURE_PRESS:
        return press;
    case KEY_GESTURE_LONG_PRESS:
        return hold;
    case KEY_GESTURE_REPEAT:
        return NOW_PLAYING_NONE;
    }
    return NOW_PLAYING_NONE;
}

uint32_t now_playing_scrub_target(uint32_t position_ms, uint32_t duration_ms, int32_t delta_ms)
{
    int64_t target = (int64_t)position_ms + delta_ms;
    if (duration_ms == 0) {
        return position_ms;
    }
    if (target < 0) {
        return 0;
    }
    return target > duration_ms ? duration_ms : (uint32_t)target;
}

static uint32_t clamp_ms(uint32_t value, uint32_t low, uint32_t high)
{
    return value < low ? low : (value > high ? high : value);
}

uint32_t now_playing_seek_step(uint32_t duration_ms)
{
    return clamp_ms(duration_ms / NOW_PLAYING_SEEK_STEP_DIVISOR, NOW_PLAYING_SEEK_STEP_MIN_MS,
                    NOW_PLAYING_SEEK_STEP_MAX_MS);
}

uint32_t now_playing_scrub_rate(uint32_t duration_ms, uint32_t held_ms)
{
    if (held_ms >= NOW_PLAYING_SCRUB_FAST_AFTER_MS) {
        return clamp_ms(duration_ms / NOW_PLAYING_SCRUB_FAST_DIVISOR, 0,
                        NOW_PLAYING_SCRUB_FAST_MAX_MS_PER_S);
    }
    return clamp_ms(duration_ms / NOW_PLAYING_SCRUB_DIVISOR, 0, NOW_PLAYING_SCRUB_MAX_MS_PER_S);
}

uint32_t now_playing_scrub_step(uint32_t duration_ms, uint32_t held_ms, uint32_t elapsed_ms)
{
    uint32_t elapsed = clamp_ms(elapsed_ms, 0, NOW_PLAYING_SCRUB_GAP_MAX_MS);
    uint32_t step = now_playing_scrub_rate(duration_ms, held_ms) * elapsed / 1000u;
    return step < NOW_PLAYING_SEEK_STEP_MIN_MS ? NOW_PLAYING_SEEK_STEP_MIN_MS : step;
}

int32_t now_playing_scrub_delta(NowPlayingScrub *scrub, uint32_t move, uint32_t duration_ms,
                                uint32_t now_ms)
{
    uint32_t elapsed = now_ms - scrub->last_ms;
    uint32_t step = now_playing_seek_step(duration_ms);
    if ((move & 1u) != 0 && elapsed <= NOW_PLAYING_SCRUB_GAP_MAX_MS) {
        step = now_playing_scrub_step(duration_ms, now_ms - scrub->started_ms, elapsed);
    } else {
        scrub->started_ms = now_ms;
    }
    scrub->last_ms = now_ms;
    return move < NOW_PLAYING_SEEK_COMMANDS / 2 ? (int32_t)step : -(int32_t)step;
}

static NowPlayingCommand chord_command_for(KeyEvent event)
{
    uint32_t back = event.key == KEY_VOLUME_UP ? 0u : 1u;
    if (event.chord == KEY_CHORD_MENU) {
        uint32_t held = event.gesture == KEY_GESTURE_REPEAT ? 1u : 0u;
        return (NowPlayingCommand)((uint32_t)NOW_PLAYING_SEEK_FORWARD + back * 2u + held);
    }
    if (event.gesture == KEY_GESTURE_REPEAT) {
        return NOW_PLAYING_NONE;
    }
    return (NowPlayingCommand)((uint32_t)NOW_PLAYING_NEXT + back);
}

NowPlayingCommand now_playing_command_for(KeyEvent event)
{
    if (event.chord != KEY_CHORD_NONE) {
        return chord_command_for(event);
    }
    switch (event.key) {
    case KEY_VOLUME_UP:
    case KEY_VOLUME_DOWN:
        return event.key == KEY_VOLUME_UP ? NOW_PLAYING_VOLUME_UP : NOW_PLAYING_VOLUME_DOWN;
    case KEY_PLAY:
        return by_gesture(event.gesture, NOW_PLAYING_TOGGLE_PAUSE, NOW_PLAYING_NONE);
    case KEY_BACK:
        return by_gesture(event.gesture, NOW_PLAYING_LEAVE, NOW_PLAYING_HOME);
    case KEY_MENU:
        return by_gesture(event.gesture, NOW_PLAYING_QUICK_SETTINGS, NOW_PLAYING_LYRICS);
    case KEY_POWER:
        return NOW_PLAYING_NONE;
    }
    return NOW_PLAYING_NONE;
}

static void seek_by(NowPlayingScreen *now_playing, uint32_t move, uint32_t now_ms)
{
    Player *player = now_playing->config.player;
    player_catch_up(player);
    PlayerStatus status = player_status(player);
    if (!status.has_track || status.duration_ms == 0) {
        return;
    }
    int32_t delta_ms =
        now_playing_scrub_delta(&now_playing->scrub, move, status.duration_ms, now_ms);
    player_seek(player, now_playing_scrub_target(status.position_ms, status.duration_ms, delta_ms));
}

void now_playing_player_command(NowPlayingScreen *now_playing, NowPlayingCommand command,
                                uint32_t now_ms)
{
    Player *player = now_playing->config.player;
    uint32_t seek = (uint32_t)command - NOW_PLAYING_SEEK_FORWARD;
    switch (command) {
    case NOW_PLAYING_TOGGLE_PAUSE:
        player_toggle_pause(player);
        return;
    case NOW_PLAYING_NEXT:
    case NOW_PLAYING_PREVIOUS:
        player_skip(player, command == NOW_PLAYING_NEXT ? 1 : -1);
        return;
    case NOW_PLAYING_VOLUME_UP:
    case NOW_PLAYING_VOLUME_DOWN:
        player_change_volume(player, command == NOW_PLAYING_VOLUME_UP ? 1 : -1);
        return;
    default:
        break;
    }
    if (seek < NOW_PLAYING_SEEK_COMMANDS) {
        seek_by(now_playing, seek, now_ms);
    }
}
