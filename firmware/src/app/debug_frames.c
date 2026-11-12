#include "app/debug_loop.h"
#include "core/frame_stats.h"
#include "debug_runner.h"
#include "draw_profile.h"

_Static_assert((int)DEBUG_FRAME_PARTS == (int)DRAW_PART_COUNT, "the report names every part");

DebugFrames app_debug_frames(void)
{
    DebugFrameSources sources = app_debug_frame_sources();
    DebugFrames frames = {.count = sources.total->recorded,
                          .total_us = frame_stats_average_us(sources.total),
                          .worst_us = frame_stats_worst_us(sources.total),
                          .update_us = frame_stats_average_us(sources.update),
                          .draw_us = frame_stats_average_us(sources.draw),
                          .send_us = frame_stats_average_us(sources.send)};
    for (int part = 0; part < DRAW_PART_COUNT; part++) {
        frames.part_us[part] = draw_profile_average_us((DrawPart)part);
    }
    return frames;
}
