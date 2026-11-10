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
    bool animating = changed && pacer->changed_last;
    if (urgent || animating) {
        pacer->fast_frames = FRAME_PACER_FAST_FRAMES;
    } else if (!changed && pacer->fast_frames > 0) {
        pacer->fast_frames--;
    }
    pacer->started = true;
    pacer->last_ms = now_ms;
    pacer->quiet_frames = changed ? 0 : pacer->quiet_frames + 1;
    pacer->changed_last = changed;
}
