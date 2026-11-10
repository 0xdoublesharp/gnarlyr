#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/debug_loop.h"
#include "core/debug_script.h"
#include "core/frame_stats.h"
#include "hal/input.h"

typedef enum DebugRequestKind {
    DEBUG_REQUEST_NONE,
    DEBUG_REQUEST_START,
    DEBUG_REQUEST_SET,
    DEBUG_REQUEST_SCREENSHOT
} DebugRequestKind;

typedef struct DebugRequest {
    DebugRequestKind kind;
    DebugSpan argument;
    DebugSpan value;
    uint32_t slot;
} DebugRequest;

typedef struct DebugFrameSources {
    const FrameStats *total;
    const FrameStats *update;
    const FrameStats *draw;
    const FrameStats *send;
} DebugFrameSources;

#ifdef ECHO_DEBUG_LOOP

bool debug_runner_active(void);
DebugRequest debug_runner_step(uint32_t now_ms, bool held);
void debug_runner_done(bool succeeded);
bool debug_runner_next_key(HalKeyTransition *transition);
void debug_runner_frame(DebugFrameSources sources);

#else

static inline bool debug_runner_active(void)
{
    return false;
}

static inline DebugRequest debug_runner_step(uint32_t now_ms, bool held)
{
    (void)now_ms;
    (void)held;
    return (DebugRequest){.kind = DEBUG_REQUEST_NONE};
}

static inline void debug_runner_done(bool succeeded)
{
    (void)succeeded;
}

static inline bool debug_runner_next_key(HalKeyTransition *transition)
{
    (void)transition;
    return false;
}

static inline void debug_runner_frame(DebugFrameSources sources)
{
    (void)sources;
}

#endif
