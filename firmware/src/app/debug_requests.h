#pragma once

#include <stdint.h>

#include "debug_runner.h"

void debug_requests_step(uint32_t now_ms, DebugRunnerView view, const DebugAppHooks *app);
