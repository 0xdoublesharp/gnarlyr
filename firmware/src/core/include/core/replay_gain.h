#pragma once

#include <stdint.h>

typedef enum ReplayGainMode {
    REPLAY_GAIN_OFF,
    REPLAY_GAIN_TRACK,
    REPLAY_GAIN_ALBUM,
    REPLAY_GAIN_MODE_COUNT
} ReplayGainMode;

enum {
    REPLAY_GAIN_UNITY = 65536
};

typedef struct ReplayGain {
    uint32_t track;
    uint32_t album;
    uint32_t track_safe;
    uint32_t album_safe;
} ReplayGain;
