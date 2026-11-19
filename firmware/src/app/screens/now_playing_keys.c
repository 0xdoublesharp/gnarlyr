#include "now_playing_screen.h"

#include "core/i18n.h"

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

static uint32_t clamp_ms(uint32_t value, uint32_t low, uint32_t high)
{
    return value < low ? low : (value > high ? high : value);
}

static uint32_t last_playable(uint32_t duration_ms)
{
    return duration_ms > NOW_PLAYING_SCRUB_TAIL_MS ? duration_ms - NOW_PLAYING_SCRUB_TAIL_MS : 0;
}

uint32_t now_playing_scrub_target(uint32_t position_ms, uint32_t duration_ms, int32_t delta_ms)
{
    int64_t target = (int64_t)position_ms + delta_ms;
    if (duration_ms == 0) {
        return position_ms;
    }
    uint32_t end = last_playable(duration_ms);
    if (delta_ms >= 0) {
        end = clamp_ms(position_ms, end, duration_ms);
    }
    return target < 0 ? 0 : clamp_ms(target > UINT32_MAX ? UINT32_MAX : (uint32_t)target, 0, end);
}

static NowPlayingCommand chord_command_for(KeyEvent event)
{
    uint32_t back = event.key == KEY_VOLUME_UP ? 0u : 1u;
    if (event.chord == KEY_CHORD_MENU) {
        return (NowPlayingCommand)((uint32_t)NOW_PLAYING_SEEK_FORWARD + back * 3u +
                                   (uint32_t)event.gesture);
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

bool now_playing_scrub_status(NowPlayingScreen *now_playing, PlayerStatus *status, uint32_t now_ms)
{
    Player *player = now_playing->config.player;
    player_catch_up(player);
    *status = player_status(player);
    bool unknown = status->duration_ms == 0;
    bool refused = status->has_track && (unknown || status->ab_repeat == 2);
    if (refused && now_playing->config.toast != NULL) {
        toast_show(now_playing->config.toast,
                   i18n_text(unknown ? STR_SCRUB_LENGTH_UNKNOWN : STR_SCRUB_AB_REPEAT_ON), now_ms);
    }
    return status->has_track && !refused;
}

static bool seek_once(NowPlayingScreen *now_playing, int32_t delta_ms, uint32_t now_ms)
{
    PlayerStatus status;
    if (!now_playing_scrub_status(now_playing, &status, now_ms)) {
        return true;
    }
    Player *player = now_playing->config.player;
    uint32_t target = now_playing_scrub_target(status.position_ms, status.duration_ms, delta_ms);
    return target == status.position_ms || player_seek_on(player, player_token(player), target);
}

void now_playing_scrub_commit(NowPlayingScreen *now_playing)
{
    NowPlayingScrub *scrub = &now_playing->scrub;
    scrub->active = false;
    if (scrub->shown_ms != scrub->base_ms) {
        player_seek_on(now_playing->config.player, scrub->token, scrub->shown_ms);
    }
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
    if (seek < NOW_PLAYING_SEEK_COMMANDS && seek % 3u == 0u) {
        int32_t delta = seek != 0u ? -NOW_PLAYING_SEEK_TAP_MS : NOW_PLAYING_SEEK_TAP_MS;
        if (!seek_once(now_playing, delta, now_ms)) {
            seek_once(now_playing, delta, now_ms);
        }
    }
}
