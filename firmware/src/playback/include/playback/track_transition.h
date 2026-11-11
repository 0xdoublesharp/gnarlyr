#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library.h"
#include "playback/play_queue.h"

enum {
    TRACK_TRANSITION_MS_PER_TENTH = 100,
    TRACK_TRANSITION_MIN_TRACK_MS = 2000,
    TRACK_TRANSITION_TRACK_DIVISOR = 3
};

typedef struct TransitionTrack {
    LibraryTrack record;
    bool spoken;
} TransitionTrack;

typedef struct TransitionRequest {
    bool fade;
    uint8_t tenths;
    ShuffleMode shuffle;
    TransitionTrack from;
    TransitionTrack to;
} TransitionRequest;

uint32_t track_transition_fade_ms(const TransitionRequest *request);
