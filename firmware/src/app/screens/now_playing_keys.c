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
    case KEY_GESTURE_DOUBLE_PRESS:
    case KEY_GESTURE_TRIPLE_PRESS:
        return NOW_PLAYING_NONE;
    }
    return NOW_PLAYING_NONE;
}

uint32_t now_playing_scrub_target(uint32_t position_ms, uint32_t duration_ms, int32_t delta_ms)
{
    int64_t target = (int64_t)position_ms + delta_ms;
    if (target < 0) {
        return 0;
    }
    return target > duration_ms ? duration_ms : (uint32_t)target;
}

static NowPlayingCommand seek_command_for(KeyEvent event)
{
    bool fast = event.repeats >= NOW_PLAYING_SEEK_FAST_AFTER_REPEATS;
    if (event.key == KEY_VOLUME_UP) {
        return fast ? NOW_PLAYING_SEEK_FORWARD_FAST : NOW_PLAYING_SEEK_FORWARD;
    }
    return fast ? NOW_PLAYING_SEEK_BACK_FAST : NOW_PLAYING_SEEK_BACK;
}

static NowPlayingCommand scrub_command_for(KeyEvent event)
{
    bool held = event.gesture == KEY_GESTURE_LONG_PRESS || event.gesture == KEY_GESTURE_REPEAT;
    switch (event.key) {
    case KEY_VOLUME_UP:
        return held ? NOW_PLAYING_SCRUB_FORWARD_FAST : NOW_PLAYING_SCRUB_FORWARD;
    case KEY_VOLUME_DOWN:
        return held ? NOW_PLAYING_SCRUB_BACK_FAST : NOW_PLAYING_SCRUB_BACK;
    case KEY_PLAY:
        return by_gesture(event.gesture, NOW_PLAYING_SCRUB_COMMIT, NOW_PLAYING_NONE);
    case KEY_BACK:
        return by_gesture(event.gesture, NOW_PLAYING_SCRUB_CANCEL, NOW_PLAYING_HOME);
    case KEY_MENU:
        return by_gesture(event.gesture, NOW_PLAYING_REPEAT_MARK, NOW_PLAYING_NONE);
    case KEY_POWER:
        return NOW_PLAYING_NONE;
    }
    return NOW_PLAYING_NONE;
}

static NowPlayingCommand chord_command_for(KeyEvent event)
{
    if (event.gesture == KEY_GESTURE_PRESS) {
        return event.key == KEY_VOLUME_UP ? NOW_PLAYING_NEXT : NOW_PLAYING_PREVIOUS;
    }
    bool held = event.gesture == KEY_GESTURE_LONG_PRESS || event.gesture == KEY_GESTURE_REPEAT;
    return held ? seek_command_for(event) : NOW_PLAYING_NONE;
}

NowPlayingCommand now_playing_command_for(KeyEvent event, NowPlayingKeys keys)
{
    if (event.chord) {
        return chord_command_for(event);
    }
    if (keys.scrubbing) {
        return scrub_command_for(event);
    }
    switch (event.key) {
    case KEY_VOLUME_UP:
    case KEY_VOLUME_DOWN:
        return event.key == KEY_VOLUME_UP ? NOW_PLAYING_VOLUME_UP : NOW_PLAYING_VOLUME_DOWN;
    case KEY_PLAY:
        return by_gesture(event.gesture, NOW_PLAYING_TOGGLE_PAUSE, NOW_PLAYING_SCRUB_START);
    case KEY_BACK:
        return by_gesture(event.gesture, NOW_PLAYING_LEAVE, NOW_PLAYING_HOME);
    case KEY_MENU:
        return by_gesture(event.gesture, NOW_PLAYING_QUICK_SETTINGS, NOW_PLAYING_LYRICS);
    case KEY_POWER:
        return NOW_PLAYING_NONE;
    }
    return NOW_PLAYING_NONE;
}

void now_playing_player_command(Player *player, NowPlayingCommand command)
{
    uint32_t seek = (uint32_t)command - NOW_PLAYING_SEEK_FORWARD;
    int32_t step = (seek & 1u) != 0 ? NOW_PLAYING_SCRUB_FAST_STEP_MS : NOW_PLAYING_SCRUB_STEP_MS;
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
        now_playing_seek_by(player, seek < NOW_PLAYING_SEEK_COMMANDS / 2 ? step : -step);
    }
}

void now_playing_seek_by(Player *player, int32_t delta_ms)
{
    PlayerStatus status = player_status(player);
    if (!status.has_track) {
        return;
    }
    player_seek(player, now_playing_scrub_target(status.position_ms, status.duration_ms, delta_ms));
}
