#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/music_fingerprint.h"

MusicFingerprint music_walk_fingerprint(uint32_t expected);
bool music_walk_run(uint32_t files);
