#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/music_fingerprint.h"

extern const char MUSIC_CHECK_PATH[];

bool music_check_unchanged(MusicFingerprint current);
void music_check_remember(MusicFingerprint print);
bool music_check_firmware_is_new(void);
void music_check_note_firmware(void);
uint32_t music_check_interrupted_scans(void);
bool music_check_covers_pending(void);
bool music_check_settled(void);
