#pragma once

#include <stdint.h>

enum {
    FRAME_STATS_WINDOW = 32
};

typedef struct FrameStats {
    uint32_t durations_us[FRAME_STATS_WINDOW];
    uint32_t recorded;
    uint32_t next_slot;
} FrameStats;

void frame_stats_reset(FrameStats *stats);
void frame_stats_record(FrameStats *stats, uint32_t duration_us);
uint32_t frame_stats_average_us(const FrameStats *stats);
uint32_t frame_stats_achievable_fps(const FrameStats *stats);

static inline uint32_t frame_stats_window_length(const FrameStats *stats)
{
    return stats->recorded < FRAME_STATS_WINDOW ? stats->recorded : FRAME_STATS_WINDOW;
}

static inline uint32_t frame_stats_latest_us(const FrameStats *stats)
{
    if (stats->recorded == 0) {
        return 0;
    }
    uint32_t latest_slot = (stats->next_slot + FRAME_STATS_WINDOW - 1) % FRAME_STATS_WINDOW;
    return stats->durations_us[latest_slot];
}

static inline uint32_t frame_stats_worst_us(const FrameStats *stats)
{
    uint32_t worst_us = 0;
    for (uint32_t slot = 0; slot < frame_stats_window_length(stats); slot++) {
        if (stats->durations_us[slot] > worst_us) {
            worst_us = stats->durations_us[slot];
        }
    }
    return worst_us;
}
