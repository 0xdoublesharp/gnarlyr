#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/music_fingerprint.h"

typedef enum MusicDriveState {
    MUSIC_DRIVE_NONE,
    MUSIC_DRIVE_KNOWN,
    MUSIC_DRIVE_UNKNOWN
} MusicDriveState;

typedef struct MusicRecord {
    MusicFingerprint card;
    MusicFingerprint internal;
    uint32_t internal_state;
    uint32_t generation;
} MusicRecord;

bool music_record_read(MusicRecord *record);
void music_record_write(const MusicRecord *record);
bool music_record_settled(void);
bool music_record_same_music(const MusicRecord *stored, const MusicRecord *current,
                             uint32_t internal_songs);
