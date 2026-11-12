#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    CARD_PRESENCE_STEADY_MS = 30
};

typedef struct CardPresence {
    uint32_t since_ms;
    bool candidate;
    bool present;
    bool ejected;
} CardPresence;

static inline bool card_presence_update(CardPresence *state, bool raw, uint32_t now)
{
    if (raw != state->candidate) {
        state->candidate = raw;
        state->since_ms = now;
    }
    if (now - state->since_ms >= CARD_PRESENCE_STEADY_MS) {
        state->present = state->candidate;
        if (!state->present) {
            state->ejected = false;
        }
    }
    return state->present;
}
