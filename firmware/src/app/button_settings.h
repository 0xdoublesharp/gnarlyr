#pragma once

#include "core/debug_script.h"
#include "settings/settings.h"
#include "ui/choice_screen.h"
#include "ui/menu_screen.h"

typedef struct ButtonSettingsConfig {
    Settings *settings;
    const Theme *theme;
    void (*changed)(void *context, bool defaults_restored);
    void *context;
} ButtonSettingsConfig;

enum {
    BUTTON_LONG_ACTION_COUNT = 6,
    BUTTON_ROW_FIRST = 1,
    BUTTON_ROW_SKIP = BUTTON_ROW_FIRST + PLAYBACK_BUTTON_COUNT,
    BUTTON_ROW_SEEK,
    BUTTON_ROW_EVERY_SCREEN,
    BUTTON_ROW_LONG,
    BUTTON_ROW_HEADSET_HOLD,
    BUTTON_ROW_RESET,
    BUTTON_ROW_COUNT,
    BUTTON_VALUE_BYTES = 64
};

typedef struct ButtonSettings ButtonSettings;

typedef struct ButtonChoices {
    const StringId *buttons;
    const StringId *actions;
    const StringId *long_actions;
    StringId long_title;
    void (*choose)(void *entry, int action);
    void (*long_choose)(void *buttons, int action);
} ButtonChoices;

typedef struct ButtonEntry {
    ButtonSettings *owner;
    PlaybackButton button;
} ButtonEntry;

struct ButtonSettings {
    ButtonSettingsConfig config;
    MenuScreen menu;
    ChoiceScreen choice;
    MenuItem items[BUTTON_ROW_COUNT];
    ButtonEntry entries[PLAYBACK_BUTTON_COUNT];
};

Screen *button_settings_init(ButtonSettings *buttons, ButtonSettingsConfig config);

#ifdef ECHO_DEBUG_LOOP
extern const ButtonChoices BUTTON_CHOICES;
#endif
