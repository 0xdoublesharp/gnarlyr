#include "ui/marquee.h"

#include <stdbool.h>

enum {
    MILLISECONDS_PER_SECOND = 1000
};

static uint32_t soonest_wait_ms;
static bool watching;
static int band_top[MARQUEE_BAND_COUNT];
static int band_bottom[MARQUEE_BAND_COUNT];

static uint32_t cycle_of(MarqueeTiming timing, int text_width, int view_width)
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
    uint64_t cycle_ms = timing.pause_ms + (scroll_ms > 0 ? scroll_ms : 1);
    return cycle_ms < UINT32_MAX ? (uint32_t)cycle_ms : UINT32_MAX;
}

static uint32_t wait_in_cycle(MarqueeTiming timing, uint32_t cycle_ms, uint32_t elapsed_ms)
{
    uint32_t phase_ms = elapsed_ms % cycle_ms;
    uint32_t moving_ms = phase_ms < timing.pause_ms ? 0 : phase_ms - timing.pause_ms;
    uint64_t step_ms =
        (uint64_t)timing.pause_ms + moving_ms - moving_ms % MARQUEE_STEP_MS + MARQUEE_STEP_MS;
    uint64_t wait_ms = (step_ms < cycle_ms ? step_ms : cycle_ms) - phase_ms;
    return wait_ms < MARQUEE_STILL ? (uint32_t)wait_ms : MARQUEE_STILL - 1;
}

uint32_t marquee_wait_ms(MarqueeTiming timing, int text_width, int view_width, uint32_t elapsed_ms)
{
    uint32_t cycle_ms = cycle_of(timing, text_width, view_width);
    return cycle_ms == 0 ? MARQUEE_STILL : wait_in_cycle(timing, cycle_ms, elapsed_ms);
}

void marquee_watch_start(void)
{
    watching = false;
    for (int band = 0; band < MARQUEE_BAND_COUNT; band++) {
        band_bottom[band] = band_top[band];
    }
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

static int offset_in_cycle(MarqueeTiming timing, uint32_t cycle_ms, uint32_t elapsed_ms)
{
    uint32_t phase_ms = elapsed_ms % cycle_ms;
    if (phase_ms < timing.pause_ms) {
        return 0;
    }
    uint32_t moving_ms = phase_ms - timing.pause_ms;
    uint64_t travelled =
        (uint64_t)(moving_ms - moving_ms % MARQUEE_STEP_MS) * timing.speed_px_per_s;
    uint64_t offset = travelled / MILLISECONDS_PER_SECOND;
    return offset < INT32_MAX ? (int)offset : INT32_MAX;
}

int marquee_offset(MarqueeTiming timing, int text_width, int view_width, uint32_t elapsed_ms)
{
    return marquee_offset_in(timing, text_width, (Rect){0, 0, view_width, 0}, elapsed_ms);
}

__attribute__((noinline)) void marquee_watch_area(MarqueeBand band, Rect area)
{
    if (area.height <= 0) {
        return;
    }
    bool first = band_bottom[band] == band_top[band];
    band_top[band] = first || area.y < band_top[band] ? area.y : band_top[band];
    int bottom = area.y + area.height;
    band_bottom[band] = first || bottom > band_bottom[band] ? bottom : band_bottom[band];
}

int marquee_offset_in(MarqueeTiming timing, int text_width, Rect area, uint32_t elapsed_ms)
{
    uint32_t cycle_ms = cycle_of(timing, text_width, area.width);
    if (cycle_ms == 0) {
        return 0;
    }
    watch(wait_in_cycle(timing, cycle_ms, elapsed_ms));
    marquee_watch_area(MARQUEE_BAND_TEXT, area);
    return offset_in_cycle(timing, cycle_ms, elapsed_ms);
}

Rect marquee_watch_band(MarqueeBand band)
{
    int height = band_bottom[band] - band_top[band];
    return (Rect){0, band_top[band], height > 0 ? 1 : 0, height};
}
