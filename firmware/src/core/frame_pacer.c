#include "core/frame_pacer.h"

#include "core/deadline.h"

static uint32_t wait_ms(const FramePacer *pacer, bool urgent)
{
    if (urgent || pacer->fast_frames > 0) {
        return FRAME_PACER_ACTIVE_MS;
    }
    return pacer->quiet_frames < FRAME_PACER_IDLE_FRAMES ? FRAME_PACER_IDLE_MS
                                                         : FRAME_PACER_REST_MS;
}

bool frame_pacer_due(const FramePacer *pacer, uint32_t now_ms, bool urgent)
{
    return !pacer->started || deadline_reached(now_ms, pacer->last_ms + wait_ms(pacer, urgent));
}

void frame_pacer_done(FramePacer *pacer, uint32_t now_ms, bool changed, bool urgent)
{
    if (!changed) {
        pacer->changed_run = 0;
    } else if (pacer->changed_run < FRAME_PACER_ANIMATION_FRAMES) {
        pacer->changed_run++;
    }
    if (urgent || pacer->changed_run == FRAME_PACER_ANIMATION_FRAMES) {
        pacer->fast_frames = FRAME_PACER_FAST_FRAMES;
    } else if (!changed && pacer->fast_frames > 0) {
        pacer->fast_frames--;
    }
    pacer->started = true;
    pacer->last_ms = now_ms;
    if (changed) {
        pacer->quiet_frames = 0;
    } else if (pacer->quiet_frames < UINT32_MAX) {
        pacer->quiet_frames++;
    }
}
