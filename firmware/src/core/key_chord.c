#include "core/key_chord.h"

static bool volume_key(Key key)
{
    return key == KEY_VOLUME_UP || key == KEY_VOLUME_DOWN;
}

KeyChordAction key_chord_take(KeyChord *chord, Key key, bool pressed, uint32_t time_ms)
{
    bool same = time_ms == chord->time_ms;
    uint8_t last = chord->last;
    chord->last = KEY_CHORD_LAST_OTHER;
    chord->time_ms = time_ms;
    if (pressed && volume_key(key) && same && last == KEY_CHORD_LAST_PLAY_RELEASE) {
        chord->play_held = true;
        return KEY_CHORD_START;
    }
    if (pressed && key == KEY_PLAY && same && last == KEY_CHORD_LAST_VOLUME_RELEASE) {
        chord->play_down = true;
        return KEY_CHORD_DROP;
    }
    if (!pressed && volume_key(key) && chord->play_held) {
        chord->last = KEY_CHORD_LAST_VOLUME_RELEASE;
        return KEY_CHORD_PASS;
    }
    chord->play_held = false;
    if (key == KEY_PLAY) {
        if (!pressed && chord->allowed && chord->play_down) {
            chord->last = KEY_CHORD_LAST_PLAY_RELEASE;
        }
        chord->play_down = pressed;
    }
    return KEY_CHORD_PASS;
}
