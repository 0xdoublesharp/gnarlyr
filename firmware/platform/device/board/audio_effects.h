#pragma once

#include <stdbool.h>

#include "device/audio_effects_module.h"

bool board_audio_effects_prepare(void);
void board_audio_effects_forget(void);
#ifdef ECHO_DEBUG_LOOP
const void *board_audio_effects_debug_code(void);
void board_audio_effects_battery_trace(DebugBatteryTrace *trace, uint32_t now_ms,
                                       PowerSample sample);
#endif
