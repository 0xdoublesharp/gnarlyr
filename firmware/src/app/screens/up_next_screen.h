#pragma once

#include <stdint.h>

#include "library/library.h"
#include "player.h"
#include "ui/list_view.h"
#include "ui/menu_screen.h"
#include "ui/screen.h"
#include "ui/theme.h"
#include "ui/toast.h"

enum {
    UP_NEXT_ROWS = 8,
    UP_NEXT_LABEL_CAPACITY = 128,
    UP_NEXT_DETAIL_CAPACITY = 64,
    UP_NEXT_QUEUE_ROWS = 500
};

typedef struct UpNextConfig {
    const Library *library;
    const Theme *theme;
    Player *player;
    Toast *toast;
} UpNextConfig;

typedef struct UpNextScreen UpNextScreen;

typedef struct UpNextEntry {
    UpNextScreen *owner;
    UpNextEdit edit;
} UpNextEntry;

struct UpNextScreen {
    Screen screen;
    UpNextConfig config;
    ListModel model;
    ListView list;
    int count;
    int added;
    int rows_first;
    int rows_count;
    char labels[UP_NEXT_ROWS][UP_NEXT_LABEL_CAPACITY];
    char details[UP_NEXT_ROWS][UP_NEXT_DETAIL_CAPACITY];
    uint32_t now_ms;
    MenuScreen actions;
    MenuItem items[UP_NEXT_EDIT_COUNT];
    UpNextEntry entries[UP_NEXT_EDIT_COUNT];
    uint32_t edit_revision;
    int edit_index;
    int edit_count;
    bool returning;
    uint32_t seen_revision;
    uint32_t seen_token;
    PlayerState seen_state;
    RepeatMode seen_repeat;
    ShuffleMode seen_shuffle;
    bool seen_has_track;
};

Screen *up_next_screen_init(UpNextScreen *up_next, UpNextConfig config);
