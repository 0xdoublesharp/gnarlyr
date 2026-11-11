#pragma once

#include "playback/store.h"

enum {
    CASSETTE_SIDE_CAPACITY = 64,
    CASSETTE_CAPACITY = 2 * CASSETTE_SIDE_CAPACITY
};

typedef struct CassetteState {
    uint32_t generation;
    uint16_t count;
    uint16_t split;
    uint8_t slot;
    uint8_t side;
    bool active;
    bool flip;
} CassetteState;

static inline bool cassette_contains(const CassetteState *state, uint32_t position)
{
    uint32_t first = state->side == 0 ? 0 : state->split;
    uint32_t end = state->side == 0 ? state->split : state->count;
    return !state->active || (position >= first && position < end);
}
bool cassette_budget(uint8_t minutes, uint32_t used_ms, uint32_t added_ms);
