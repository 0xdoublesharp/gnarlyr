#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/replay_gain.h"
#include "library/library.h"

bool library_read_replay_gain(const Library *library, uint32_t index, ReplayGain *gain);
