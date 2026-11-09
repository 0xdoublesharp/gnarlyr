#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    FRAME_PACER_ACTIVE_MS = 16,
    FRAME_PACER_IDLE_MS = 100,
    FRAME_PACER_QUIET_FRAMES = 6
};

typedef struct FramePacer {
    uint32_t last_ms;
    uint32_t quiet_frames;
    bool started;
} FramePacer;

bool frame_pacer_due(const FramePacer *pacer, uint32_t now_ms, bool urgent);
void frame_pacer_done(FramePacer *pacer, uint32_t now_ms, bool changed);
