#include "core/frame_stats.h"
#include "core/seam.h"

enum {
    MICROSECONDS_PER_SECOND = 1000000
};

void frame_stats_reset(FrameStats *stats)
{
    *stats = (FrameStats){0};
}

CORE_SEAM void frame_stats_record(FrameStats *stats, uint32_t duration_us)
{
    stats->durations_us[stats->next_slot] = duration_us;
    stats->next_slot = (stats->next_slot + 1) % FRAME_STATS_WINDOW;
    if (stats->recorded < UINT32_MAX) {
        stats->recorded++;
    }
}

uint32_t frame_stats_average_us(const FrameStats *stats)
{
    uint32_t length = frame_stats_window_length(stats);
    if (length == 0) {
        return 0;
    }
    uint64_t total_us = 0;
    for (uint32_t slot = 0; slot < length; slot++) {
        total_us += stats->durations_us[slot];
    }
    return (uint32_t)(total_us / length);
}

uint32_t frame_stats_achievable_fps(const FrameStats *stats)
{
    uint32_t average_us = frame_stats_average_us(stats);
    return average_us == 0 ? 0 : MICROSECONDS_PER_SECOND / average_us;
}
