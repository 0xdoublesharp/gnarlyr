#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/key.h"

typedef enum KeyChordAction {
    KEY_CHORD_PASS,
    KEY_CHORD_DROP,
    KEY_CHORD_START
} KeyChordAction;

typedef enum KeyChordLast {
    KEY_CHORD_LAST_OTHER,
    KEY_CHORD_LAST_PLAY_RELEASE,
    KEY_CHORD_LAST_VOLUME_RELEASE
} KeyChordLast;

typedef struct KeyChord {
    bool allowed;
    bool play_held;
    uint8_t last;
    uint32_t time_ms;
} KeyChord;

KeyChordAction key_chord_take(KeyChord *chord, Key key, bool pressed, uint32_t time_ms);
