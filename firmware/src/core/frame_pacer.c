#include "core/frame_pacer.h"

#include "core/deadline.h"

bool frame_pacer_due(const FramePacer *pacer, uint32_t now_ms, bool urgent)
{
    bool quiet = !urgent && pacer->quiet_frames >= FRAME_PACER_QUIET_FRAMES;
    uint32_t wait_ms = quiet ? FRAME_PACER_IDLE_MS : FRAME_PACER_ACTIVE_MS;
    return !pacer->started || deadline_reached(now_ms, pacer->last_ms + wait_ms);
}

void frame_pacer_done(FramePacer *pacer, uint32_t now_ms, bool changed)
{
    pacer->started = true;
    pacer->last_ms = now_ms;
    pacer->quiet_frames = changed ? 0 : pacer->quiet_frames + 1;
}
