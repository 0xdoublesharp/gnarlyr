#pragma once

#include <stdint.h>

#include "app/rescan_module.h"

extern const char LIBRARY_SCRATCH_PATH[];
extern const char RESCAN_TIMES_PATH[];

RescanOutcome library_rescan_run(RescanProgress progress);
const char *library_rescan_message(RescanOutcome outcome, char *text, uint32_t capacity);
