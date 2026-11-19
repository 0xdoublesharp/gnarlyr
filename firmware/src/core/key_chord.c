#include "core/key_chord.h"

static bool volume_key(Key key)
{
    return key == KEY_VOLUME_UP || key == KEY_VOLUME_DOWN;
}

static uint8_t seed_of(Key key)
{
    if (key == KEY_PLAY) {
        return KEY_CHORD_PLAY;
    }
    return key == KEY_MENU ? KEY_CHORD_MENU : KEY_CHORD_NONE;
}

static void follow_seed(KeyChord *chord, uint8_t seed, bool pressed)
{
    if (!pressed && chord->allowed && chord->down == seed) {
        chord->last = seed;
    }
    chord->down = pressed ? seed : KEY_CHORD_NONE;
}

KeyChordAction key_chord_take(KeyChord *chord, Key key, bool pressed, uint32_t time_ms)
{
    if (key == KEY_POWER) {
        return KEY_CHORD_PASS;
    }
    bool same = time_ms == chord->time_ms;
    uint8_t last = chord->last;
    uint8_t seed = seed_of(key);
    chord->last = KEY_CHORD_LAST_OTHER;
    chord->time_ms = time_ms;
    bool seed_released = last == KEY_CHORD_LAST_PLAY_RELEASE || last == KEY_CHORD_LAST_MENU_RELEASE;
    if (pressed && volume_key(key) && same && seed_released) {
        chord->held = last;
        return KEY_CHORD_START;
    }
    if (pressed && seed == chord->held && same && last == KEY_CHORD_LAST_VOLUME_RELEASE) {
        chord->down = seed;
        return KEY_CHORD_DROP;
    }
    if (!pressed && volume_key(key) && chord->held != KEY_CHORD_NONE) {
        chord->last = KEY_CHORD_LAST_VOLUME_RELEASE;
        return KEY_CHORD_PASS;
    }
    chord->held = KEY_CHORD_NONE;
    if (seed != KEY_CHORD_NONE) {
        follow_seed(chord, seed, pressed);
    }
    return KEY_CHORD_PASS;
}
