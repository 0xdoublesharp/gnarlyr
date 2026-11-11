#include "ui/marquee.h"

#include <limits.h>

enum {
    MILLISECONDS_PER_SECOND = 1000
};

int marquee_offset(MarqueeTiming timing, int text_width, int view_width, uint32_t elapsed_ms)
{
    if (text_width <= view_width || timing.speed_px_per_s == 0) {
        return 0;
    }
    int64_t signed_distance = (int64_t)text_width + timing.gap_px;
    if (signed_distance <= 0) {
        return 0;
    }
    uint64_t distance_px = (uint64_t)signed_distance;
    uint64_t scroll_ms = distance_px * MILLISECONDS_PER_SECOND / timing.speed_px_per_s;
    scroll_ms = scroll_ms > 0 ? scroll_ms : 1;
    uint64_t cycle_ms = timing.pause_ms + scroll_ms;
    uint64_t phase_ms = elapsed_ms % cycle_ms;
    if (phase_ms < timing.pause_ms) {
        return 0;
    }
    uint64_t travelled = (phase_ms - timing.pause_ms) * timing.speed_px_per_s;
    uint64_t offset = travelled / MILLISECONDS_PER_SECOND;
    return offset < INT_MAX ? (int)offset : INT_MAX;
}
