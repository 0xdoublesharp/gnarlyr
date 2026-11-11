#pragma once

#include <stdint.h>

#include "debug_runner.h"

DebugRequest debug_steps_step(uint32_t now_ms, DebugRunnerView view);
void debug_steps_stop(uint32_t now_ms, DebugScriptState state);
void debug_steps_frame(DebugFrameSources sources);
