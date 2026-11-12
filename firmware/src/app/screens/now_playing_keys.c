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

static NowPlayingCommand volume_command_for(KeyEvent event, NowPlayingCommand volume,
                                            NowPlayingCommand skip)
{
    switch (event.gesture) {
    case KEY_GESTURE_PRESS:
        return volume;
    case KEY_GESTURE_LONG_PRESS:
    case KEY_GESTURE_DOUBLE_PRESS:
        return skip;
    case KEY_GESTURE_REPEAT:
    case KEY_GESTURE_TRIPLE_PRESS:
        return NOW_PLAYING_NONE;
    }
    return NOW_PLAYING_NONE;
}

static NowPlayingCommand play_command_for(KeyEvent event, SkipGesture skip)
{
    if (skip == SKIP_GESTURE_PLAY_CLICKS) {
        if (event.gesture == KEY_GESTURE_DOUBLE_PRESS) {
            return NOW_PLAYING_NEXT;
        }
        if (event.gesture == KEY_GESTURE_TRIPLE_PRESS) {
            return NOW_PLAYING_PREVIOUS;
        }
    }
    return by_gesture(event.gesture, NOW_PLAYING_TOGGLE_PAUSE, NOW_PLAYING_SCRUB_START);
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

static bool holds_volume(KeyEvent event)
{
    bool volume = event.key == KEY_VOLUME_UP || event.key == KEY_VOLUME_DOWN;
    return volume &&
           (event.gesture == KEY_GESTURE_LONG_PRESS || event.gesture == KEY_GESTURE_REPEAT);
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

NowPlayingCommand now_playing_command_for(KeyEvent event, NowPlayingKeys keys)
{
    if (keys.scrubbing) {
        return scrub_command_for(event);
    }
    if (keys.adjusting_volume && holds_volume(event)) {
        return event.key == KEY_VOLUME_UP ? NOW_PLAYING_VOLUME_UP : NOW_PLAYING_VOLUME_DOWN;
    }
    if (keys.skip == SKIP_GESTURE_DOUBLE_VOLUME && holds_volume(event)) {
        return seek_command_for(event);
    }
    if (event.gesture == KEY_GESTURE_REPEAT && keys.skip != SKIP_GESTURE_HOLD &&
        (event.key == KEY_VOLUME_UP || event.key == KEY_VOLUME_DOWN)) {
        return event.key == KEY_VOLUME_UP ? NOW_PLAYING_VOLUME_UP : NOW_PLAYING_VOLUME_DOWN;
    }
    switch (event.key) {
    case KEY_VOLUME_UP:
        return volume_command_for(event, NOW_PLAYING_VOLUME_UP, NOW_PLAYING_NEXT);
    case KEY_VOLUME_DOWN:
        return volume_command_for(event, NOW_PLAYING_VOLUME_DOWN, NOW_PLAYING_PREVIOUS);
    case KEY_PLAY:
        return play_command_for(event, keys.skip);
    case KEY_BACK:
        return by_gesture(event.gesture, NOW_PLAYING_LEAVE, NOW_PLAYING_HOME);
    case KEY_MENU:
        return by_gesture(event.gesture, NOW_PLAYING_QUICK_SETTINGS, NOW_PLAYING_LYRICS);
    case KEY_POWER:
        return NOW_PLAYING_NONE;
    }
    return NOW_PLAYING_NONE;
}

bool now_playing_adjusting_volume(uint32_t volume_changed_ms, uint32_t now_ms)
{
    return now_ms - volume_changed_ms < NOW_PLAYING_VOLUME_HOLD_MS;
}

void now_playing_seek_by(Player *player, int32_t delta_ms)
{
    PlayerStatus status = player_status(player);
    if (!status.has_track) {
        return;
    }
    player_seek(player, now_playing_scrub_target(status.position_ms, status.duration_ms, delta_ms));
}
