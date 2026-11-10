#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/key.h"
#include "core/key_script.h"

enum {
    SIM_SCRIPT_MAX_STEPS = 64,
    SIM_SCRIPT_MAX_EVENTS = SIM_SCRIPT_MAX_STEPS * 2,
    SIM_SCRIPT_FRAME_MS = 16,
    SIM_SCRIPT_TAP_FRAMES = KEY_SCRIPT_TAP_MS / SIM_SCRIPT_FRAME_MS,
    SIM_SCRIPT_HOLD_FRAMES = KEY_SCRIPT_LONG_MS / SIM_SCRIPT_FRAME_MS,
    SIM_SCRIPT_HELD_FRAMES = KEY_SCRIPT_HELD_MS / SIM_SCRIPT_FRAME_MS,
    SIM_SCRIPT_GAP_FRAMES = KEY_SCRIPT_GAP_MS / SIM_SCRIPT_FRAME_MS
};

typedef struct SimScript {
    KeyScriptStep steps[SIM_SCRIPT_MAX_STEPS];
    int count;
} SimScript;

typedef struct SimScriptEvent {
    uint32_t frame;
    Key key;
    bool pressed;
} SimScriptEvent;

bool sim_script_parse(const char *text, SimScript *script);
int sim_script_schedule(const SimScript *script, uint32_t first_frame, SimScriptEvent *events);
