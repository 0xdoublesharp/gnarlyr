#include "app/debug_loop.h"

#include <stddef.h>

#include "debug_loop_internal.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/input.h"
#include "hal/system.h"

enum {
    FOLLOW_INTERVAL_MS = 100,
    GESTURE_SHIFT = 8,
    TRACK_SHIFT = 8,
    STATE_MASK = 0xFF
};

typedef struct DebugLoop {
    DebugLog log;
    uint32_t player_state;
    uint32_t refusals;
    uint32_t underruns;
    uint32_t output_underruns;
    uint32_t followed_ms;
    uint32_t shutdown_reason;
} DebugLoop;

static DebugLoop loop;
static uint32_t boot_us[DEBUG_BOOT_COUNT];

void debug_loop_boot(DebugBootStep step)
{
    if (boot_us[step] == 0) {
        boot_us[step] = hal_clock_microseconds() + 1;
    }
}

const uint32_t *debug_loop_boot_times(void)
{
    return boot_us;
}

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
        record(DEBUG_EVENT_KEY, value | (uint32_t)hal_input_key_adc() << 16, NULL);
    }
}

void debug_loop_screen(const char *title, int depth)
{
    hal_system_usb_connect(false);
    record(DEBUG_EVENT_SCREEN, (uint32_t)depth, title);
}

static void follow_player(const Player *player)
{
    uint32_t track = player->has_track ? player->track : 0u;
    uint32_t shown = player_in_standby(player) ? DEBUG_PLAYER_STANDBY : (uint32_t)player->state;
    uint32_t state = shown | track << TRACK_SHIFT;
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

void debug_loop_shutdown(DebugReportReason reason)
{
    debug_runner_abort();
    loop.shutdown_reason = reason;
}

uint32_t debug_loop_take_shutdown(void)
{
    uint32_t reason = loop.shutdown_reason;
    loop.shutdown_reason = DEBUG_REPORT_NONE;
    return reason;
}

void debug_loop_save_report(void)
{
    uint32_t reason = debug_loop_take_shutdown();
    if (reason != DEBUG_REPORT_NONE) {
        hal_system_save_report(reason);
    }
}

bool debug_loop_playing(void)
{
    return (loop.player_state & STATE_MASK) == PLAYER_PLAYING;
}

const DebugLog *debug_loop_log(void)
{
    return &loop.log;
}
