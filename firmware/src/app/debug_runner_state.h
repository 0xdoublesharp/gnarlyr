#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/debug_script.h"
#include "core/key.h"
#include "hal/input.h"

typedef enum DebugRunnerPhase {
    DEBUG_PHASE_NEXT,
    DEBUG_PHASE_KEY_HELD,
    DEBUG_PHASE_KEY_GAP,
    DEBUG_PHASE_WAITING,
    DEBUG_PHASE_MEASURING,
    DEBUG_PHASE_REQUESTED,
    DEBUG_PHASE_SOAKING
} DebugRunnerPhase;

typedef struct DebugRunnerState {
    DebugScriptRun run;
    uint8_t next_op;
    uint8_t line;
    uint8_t phase;
    uint8_t requested;
    Key key;
    bool key_down;
    bool reconnect_due;
    bool transition_ready;
    HalKeyTransition transition;
    uint32_t until_ms;
    uint32_t underruns_before;
    bool screen_on;
    bool key_pressed;
    uint8_t low_checks;
    uint32_t checked_ms;
    uint32_t idle_us;
} DebugRunnerState;

extern DebugRunnerState debug_runner;

void debug_runner_note_state(DebugScriptState state);
void debug_runner_set_status(DebugLineStatus status);
void debug_runner_finish(uint32_t now_ms, DebugScriptState state);
