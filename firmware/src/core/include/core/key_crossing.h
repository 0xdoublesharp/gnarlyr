#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/key.h"

enum {
    KEY_CROSSING_WINDOW_MS = 150,
    KEY_CROSSING_OUT_CAPACITY = 4
};

typedef struct KeyStep {
    Key key;
    bool pressed;
    uint32_t time_ms;
} KeyStep;

typedef struct KeyCrossingOut {
    KeyStep steps[KEY_CROSSING_OUT_CAPACITY];
    uint8_t count;
} KeyCrossingOut;

typedef struct KeyCrossingRules {
    bool menu;
    bool volume;
} KeyCrossingRules;

typedef struct KeyCrossing {
    uint8_t state;
    uint8_t from;
    uint32_t from_ms;
    uint32_t play_ms;
} KeyCrossing;

void key_crossing_reset(KeyCrossing *crossing);
void key_crossing_take(KeyCrossing *crossing, KeyStep step, KeyCrossingRules rules,
                       KeyCrossingOut *out);
void key_crossing_settle(KeyCrossing *crossing, uint32_t now_ms, KeyCrossingOut *out);
