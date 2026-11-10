#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    FRAME_PACER_ACTIVE_MS = 16,
    FRAME_PACER_IDLE_MS = 100,
    FRAME_PACER_REST_MS = 1000,
    FRAME_PACER_FAST_FRAMES = 6,
    FRAME_PACER_IDLE_FRAMES = 2
};

typedef struct FramePacer {
    uint32_t last_ms;
    uint32_t quiet_frames;
    uint8_t fast_frames;
    bool changed_last;
    bool started;
} FramePacer;

bool frame_pacer_due(const FramePacer *pacer, uint32_t now_ms, bool urgent);
void frame_pacer_done(FramePacer *pacer, uint32_t now_ms, bool changed, bool urgent);
