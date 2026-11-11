#pragma once

#include "player.h"
#include "settings_store.h"
#include "ui/choice_screen.h"
#include "ui/menu_screen.h"
#include "ui/toast.h"

enum {
    PLAYING_OPTIONS_SPEED_ITEM,
    PLAYING_OPTIONS_LYRICS_ITEM,
    PLAYING_OPTIONS_ITEM_COUNT
};

typedef struct PlayingOptionsConfig {
    Player *player;
    SettingsStore *store;
    Toast *toast;
    Screen *lyrics;
    const Theme *theme;
} PlayingOptionsConfig;

typedef struct PlayingOptions {
    PlayingOptionsConfig config;
    MenuScreen menu;
    MenuItem items[PLAYING_OPTIONS_ITEM_COUNT];
    ChoiceScreen speed;
} PlayingOptions;

Screen *playing_options_init(PlayingOptions *options, PlayingOptionsConfig config);
