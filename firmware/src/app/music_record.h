#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/music_fingerprint.h"

bool music_record_unchanged(MusicFingerprint current);
uint32_t music_record_entries(void);
void music_record_remember(MusicFingerprint print);
