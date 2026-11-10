#include "app/debug_loop.h"

#include <stddef.h>

#include "debug_loop_internal.h"
#include "hal/audio.h"
#include "hal/clock.h"

enum {
    FOLLOW_INTERVAL_MS = 100,
    GESTURE_SHIFT = 8,
    TRACK_SHIFT = 8
};

typedef struct DebugLoop {
    DebugLog log;
    uint32_t player_state;
    uint32_t refusals;
    uint32_t underruns;
    uint32_t output_underruns;
    uint32_t followed_ms;
} DebugLoop;

static DebugLoop loop;

static void record(DebugEventKind kind, uint32_t value, const char *text)
{
    DebugEvent event = debug_event_make(hal_clock_milliseconds(), kind, value, text);
    debug_log_record(&loop.log, &event);
}

void debug_loop_event(DebugEventKind kind, uint32_t value)
{
    record(kind, value, NULL);
}

void debug_loop_key(KeyEvent event)
{
    if (event.gesture != KEY_GESTURE_REPEAT) {
        uint32_t value = (uint32_t)event.key | (uint32_t)event.gesture << GESTURE_SHIFT;
        record(DEBUG_EVENT_KEY, value, NULL);
    }
}

void debug_loop_screen(const char *title, int depth)
{
    record(DEBUG_EVENT_SCREEN, (uint32_t)depth, title);
}

static void follow_player(const Player *player)
{
    uint32_t track = player->has_track ? player->track : 0u;
    uint32_t state = (uint32_t)player->state | track << TRACK_SHIFT;
    if (state != loop.player_state) {
        record(DEBUG_EVENT_PLAYER, state, NULL);
    }
    if (player->refusal_count != loop.refusals) {
        record(DEBUG_EVENT_REFUSAL, (uint32_t)player->refusal.problem, NULL);
    }
    loop.player_state = state;
    loop.refusals = player->refusal_count;
}

static void follow_audio(void)
{
    HalAudioStats stats = hal_audio_stats();
    if (stats.underruns > loop.underruns) {
        record(DEBUG_EVENT_UNDERRUN, stats.underruns, NULL);
    }
    if (stats.output_underruns > loop.output_underruns) {
        record(DEBUG_EVENT_OUTPUT_UNDERRUN, stats.output_underruns, NULL);
    }
    loop.underruns = stats.underruns;
    loop.output_underruns = stats.output_underruns;
}

void debug_loop_follow(uint32_t now_ms, const Player *player)
{
    follow_player(player);
    if (now_ms - loop.followed_ms >= FOLLOW_INTERVAL_MS) {
        loop.followed_ms = now_ms;
        follow_audio();
    }
}

const DebugLog *debug_loop_log(void)
{
    return &loop.log;
}
