#pragma once

#include <stdbool.h>

bool board_audio_process_prepare(void);
void board_audio_process_forget(void);
#ifdef ECHO_DEBUG_LOOP
const void *board_audio_process_debug_code(void);
#endif
