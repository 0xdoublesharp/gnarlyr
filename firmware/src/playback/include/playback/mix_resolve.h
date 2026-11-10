#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/mix_tape.h"
#include "playback/track_order.h"

typedef struct MixMemory {
    void *bytes;
    uint32_t size;
} MixMemory;

typedef struct MixResolve {
    const Library *library;
    MixTapeCursor cursor;
    uint16_t *slots;
    uint16_t *tracks;
    uint32_t capacity;
    uint32_t position;
    uint32_t hash;
    uint32_t probe;
    MixIdentity wanted;
    uint16_t count;
    uint16_t missing;
    uint16_t ambiguous;
    uint16_t track_limit;
    uint16_t found;
    uint8_t matches;
    uint8_t stage;
    bool active;
    StoreResult result;
} MixResolve;

bool mix_resolve_begin(MixResolve *job, const Library *library, const MixTape *tape,
                       MixMemory memory);
StoreResult mix_resolve_step(MixResolve *job);
TrackOrderSource mix_resolve_source(MixResolve *job);
