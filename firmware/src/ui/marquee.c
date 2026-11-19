#include "ui/marquee.h"

#include <stdbool.h>

enum {
    MILLISECONDS_PER_SECOND = 1000
};

static uint32_t soonest_wait_ms;
static bool watching;

static uint64_t cycle_of(MarqueeTiming timing, int text_width, int view_width)
{
    if (text_width <= view_width || timing.speed_px_per_s == 0) {
        return 0;
    }
    int64_t signed_distance = (int64_t)text_width + timing.gap_px;
    if (signed_distance <= 0) {
        return 0;
    }
    uint64_t scroll_ms =
        (uint64_t)signed_distance * MILLISECONDS_PER_SECOND / timing.speed_px_per_s;
    return timing.pause_ms + (scroll_ms > 0 ? scroll_ms : 1);
}

uint32_t marquee_wait_ms(MarqueeTiming timing, int text_width, int view_width, uint32_t elapsed_ms)
{
    uint64_t cycle_ms = cycle_of(timing, text_width, view_width);
    if (cycle_ms == 0) {
        return MARQUEE_STILL;
    }
    uint64_t phase_ms = elapsed_ms % cycle_ms;
    uint64_t moving_ms = phase_ms < timing.pause_ms ? 0 : phase_ms - timing.pause_ms;
    uint64_t step_ms = timing.pause_ms + moving_ms - moving_ms % MARQUEE_STEP_MS + MARQUEE_STEP_MS;
    uint64_t wait_ms = (step_ms < cycle_ms ? step_ms : cycle_ms) - phase_ms;
    return wait_ms < MARQUEE_STILL ? (uint32_t)wait_ms : MARQUEE_STILL - 1;
}

void marquee_watch_start(void)
{
    watching = false;
}

uint32_t marquee_watch_wait_ms(void)
{
    return watching ? soonest_wait_ms : MARQUEE_STILL;
}

static void watch(uint32_t wait_ms)
{
    if (!watching || wait_ms < soonest_wait_ms) {
        soonest_wait_ms = wait_ms;
        watching = true;
    }
}

int marquee_offset(MarqueeTiming timing, int text_width, int view_width, uint32_t elapsed_ms)
{
    uint64_t cycle_ms = cycle_of(timing, text_width, view_width);
    if (cycle_ms == 0) {
        return 0;
    }
    watch(marquee_wait_ms(timing, text_width, view_width, elapsed_ms));
    uint64_t phase_ms = elapsed_ms % cycle_ms;
    if (phase_ms < timing.pause_ms) {
        return 0;
    }
    uint64_t moving_ms = phase_ms - timing.pause_ms;
    uint64_t travelled = (moving_ms - moving_ms % MARQUEE_STEP_MS) * timing.speed_px_per_s;
    uint64_t offset = travelled / MILLISECONDS_PER_SECOND;
    return offset < INT32_MAX ? (int)offset : INT32_MAX;
}
