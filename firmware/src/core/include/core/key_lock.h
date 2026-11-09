#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/key.h"

enum {
    KEY_LOCK_HOLD_MS = 1000
};

typedef enum KeyLockVerdict {
    KEY_LOCK_PASS,
    KEY_LOCK_SWALLOW,
    KEY_LOCK_COMBO_STARTED,
    KEY_LOCK_REFUSED
} KeyLockVerdict;

typedef struct KeyLock {
    bool locked;
    bool combo;
    bool combo_done;
    uint32_t combo_since_ms;
    bool held[KEY_COUNT];
} KeyLock;

KeyLockVerdict key_lock_transition(KeyLock *lock, Key key, bool pressed, uint32_t now_ms);
bool key_lock_update(KeyLock *lock, uint32_t now_ms);
