#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/debug_log.h"
#include "core/debug_script.h"

enum {
    DEBUG_STORAGE_FULL = 1,
    DEBUG_STORAGE_SCAN_FAILED = 0x100
};

DebugFrames app_debug_frames(void);

#ifdef ECHO_DEBUG_LOOP

void debug_loop_event(DebugEventKind kind, uint32_t value);
const DebugLog *debug_loop_log(void);
bool debug_loop_playing(void);
uint32_t debug_loop_take_shutdown(void);
DebugScriptRun *debug_runner_script(void);
void debug_runner_begin(void);
void debug_runner_abort(void);

#else

static inline void debug_loop_event(DebugEventKind kind, uint32_t value)
{
    (void)kind;
    (void)value;
}

#endif
