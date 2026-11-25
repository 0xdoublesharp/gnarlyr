#pragma once

#include "device/dac_eq.h"

bool board_audio_encode_eq(const Equalizer *equalizer, uint32_t rate, DacEqProgram *program);

#include <stdbool.h>

#include "device/audio_effects_module.h"

bool board_audio_effects_prepare(void);
void board_audio_effects_forget(void);
#ifdef ECHO_DEBUG_LOOP
const void *board_audio_effects_debug_code(void);
void board_audio_effects_battery_trace(DebugBatteryTrace *trace, uint32_t now_ms,
                                       PowerSample sample);
#endif
