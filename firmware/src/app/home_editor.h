#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/i18n.h"
#include "settings/settings.h"
#include "ui/icons.h"
#include "ui/list_view.h"
#include "ui/menu_screen.h"
#include "ui/screen.h"
#include "ui/theme.h"
#include "ui/toast.h"

enum {
    HOME_EDITOR_ROWS = SETTINGS_HOME_ITEM_COUNT + 1,
    HOME_EDITOR_ACTION_COUNT = 2,
    HOME_EDITOR_TEXT_CAPACITY = 64
};

typedef struct HomeItem {
    StringId label;
    IconId icon;
} HomeItem;

typedef struct HomeEditorConfig {
    Settings *settings;
    const HomeItem *items;
    const Theme *theme;
    Toast *toast;
    bool (*changed)(void *context);
    void *context;
} HomeEditorConfig;

typedef struct HomeEditor {
    Screen screen;
    HomeEditorConfig config;
    ListModel model;
    ListView list;
    MenuScreen actions;
    MenuItem action_items[HOME_EDITOR_ACTION_COUNT];
    uint8_t order[SETTINGS_HOME_ITEM_COUNT];
    int8_t target;
    int8_t picked_from;
    bool moving;
    bool pick_pending;
    uint32_t now_ms;
    char message[HOME_EDITOR_TEXT_CAPACITY];
} HomeEditor;

Screen *home_editor_init(HomeEditor *editor, HomeEditorConfig config);
