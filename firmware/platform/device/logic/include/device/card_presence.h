#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    CARD_PRESENCE_STEADY_MS = 30,
    CARD_START_RETRY_MS = 1000,
    CARD_START_TRIES = 10
};

typedef struct CardStart {
    uint32_t tried_ms;
    uint8_t tries;
} CardStart;

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

static inline bool card_start_due(CardStart *start, uint32_t now)
{
    bool due = start->tries == 0 ||
               (start->tries < CARD_START_TRIES && now - start->tried_ms >= CARD_START_RETRY_MS);
    if (due) {
        start->tries++;
        start->tried_ms = now;
    }
    return due;
}
