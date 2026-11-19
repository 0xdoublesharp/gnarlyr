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

uint32_t now_playing_seek_step(uint32_t duration_ms, bool fast)
{
    uint32_t factor = fast ? NOW_PLAYING_SEEK_FAST_FACTOR : 1u;
    uint32_t step = duration_ms / NOW_PLAYING_SEEK_STEP_DIVISOR * factor;
    uint32_t low = NOW_PLAYING_SEEK_STEP_MIN_MS * factor;
    uint32_t high = fast ? NOW_PLAYING_SEEK_FAST_MAX_MS : NOW_PLAYING_SEEK_STEP_MAX_MS;
    return step < low ? low : (step > high ? high : step);
}

int32_t now_playing_seek_delta(uint32_t move, uint32_t duration_ms)
{
    int32_t step = (int32_t)now_playing_seek_step(duration_ms, (move & 1u) != 0);
    return move < NOW_PLAYING_SEEK_COMMANDS / 2 ? step : -step;
}

static NowPlayingCommand chord_command_for(KeyEvent event)
{
    uint32_t back = event.key == KEY_VOLUME_UP ? 0u : 1u;
    if (event.chord == KEY_CHORD_MENU) {
        uint32_t fast = event.repeats >= NOW_PLAYING_SEEK_FAST_AFTER_REPEATS ? 1u : 0u;
        return (NowPlayingCommand)((uint32_t)NOW_PLAYING_SEEK_FORWARD + back * 2u + fast);
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

static void seek_by(Player *player, uint32_t move)
{
    player_catch_up(player);
    PlayerStatus status = player_status(player);
    if (!status.has_track || status.duration_ms == 0) {
        return;
    }
    int32_t delta_ms = now_playing_seek_delta(move, status.duration_ms);
    player_seek(player, now_playing_scrub_target(status.position_ms, status.duration_ms, delta_ms));
}

void now_playing_player_command(Player *player, NowPlayingCommand command)
{
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
        seek_by(player, seek);
    }
}
