#include "core/key_lock.h"

#include "core/deadline.h"

static bool combo_key(Key key)
{
    return key == KEY_PLAY || key == KEY_POWER;
}

static Key partner_of(Key key)
{
    return key == KEY_PLAY ? KEY_POWER : KEY_PLAY;
}

static bool shortcut_key(Key key)
{
#ifdef ECHO_DEBUG_LOOP
    if (key == KEY_MENU) {
        return true;
    }
#endif
    return key == KEY_VOLUME_UP || key == KEY_VOLUME_DOWN;
}

KeyLockVerdict key_lock_transition(KeyLock *lock, Key key, bool pressed, uint32_t now_ms)
{
    bool was_held = lock->held[key];
    lock->held[key] = pressed;
    if (lock->shortcut != 0) {
        bool held = lock->held[KEY_POWER] || lock->held[lock->shortcut - 1];
        lock->shortcut = held ? lock->shortcut : 0;
        return KEY_LOCK_SWALLOW;
    }
    if (!lock->combo && pressed && !was_held && lock->held[KEY_POWER] && shortcut_key(key)) {
        lock->shortcut = (uint8_t)key + 1;
        return KEY_LOCK_SHORTCUT;
    }
    if (combo_key(key)) {
        if (pressed && !was_held && lock->held[partner_of(key)] && !lock->combo) {
            lock->combo = true;
            lock->combo_done = false;
            lock->combo_since_ms = now_ms;
            return KEY_LOCK_COMBO_STARTED;
        }
        if (lock->combo) {
            if (pressed && !was_held && lock->held[partner_of(key)] && !lock->combo_done) {
                lock->combo_since_ms = now_ms;
            }
            lock->combo = lock->held[KEY_PLAY] || lock->held[KEY_POWER];
            return KEY_LOCK_SWALLOW;
        }
    }
    if (!lock->locked || key == KEY_POWER) {
        return KEY_LOCK_PASS;
    }
    return pressed && !was_held ? KEY_LOCK_REFUSED : KEY_LOCK_SWALLOW;
}

bool key_lock_update(KeyLock *lock, uint32_t now_ms)
{
    bool both = lock->held[KEY_PLAY] && lock->held[KEY_POWER];
    if (!lock->combo || lock->combo_done || !both ||
        !deadline_reached(now_ms, lock->combo_since_ms + KEY_LOCK_HOLD_MS)) {
        return false;
    }
    lock->combo_done = true;
    lock->locked = !lock->locked;
    return true;
}
