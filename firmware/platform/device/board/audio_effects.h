#pragma once

#include <stdbool.h>

bool board_audio_effects_prepare(void);
void board_audio_effects_forget(void);
#ifdef ECHO_DEBUG_LOOP
const void *board_audio_effects_debug_code(void);
#endif
