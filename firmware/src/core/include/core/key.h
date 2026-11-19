#pragma once

typedef enum Key {
    KEY_VOLUME_UP,
    KEY_VOLUME_DOWN,
    KEY_PLAY,
    KEY_MENU,
    KEY_BACK,
    KEY_POWER
} Key;

typedef enum KeyChordSeed {
    KEY_CHORD_NONE,
    KEY_CHORD_PLAY,
    KEY_CHORD_MENU
} KeyChordSeed;

enum {
    KEY_COUNT = KEY_POWER + 1
};
