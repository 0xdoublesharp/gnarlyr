#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "settings/equalizer_slots.h"
#include "settings/settings.h"
#include "ui/list_view.h"
#include "ui/screen.h"
#include "ui/theme.h"

enum {
    EQUALIZER_SCREEN_ROWS = SETTINGS_EQUALIZER_BANDS + 1,
    EQUALIZER_SCREEN_TEXT_CAPACITY = 16,
    EQUALIZER_SCREEN_TITLE_CAPACITY = 24
};

typedef struct EqualizerScreenConfig {
    Settings *settings;
    void (*changed)(void *context, bool finished);
    bool (*slot_used)(const void *context, int slot);
    void (*save_slot)(void *context, int slot);
    void *context;
    const Theme *theme;
    int slot;
} EqualizerScreenConfig;

typedef struct EqualizerScreen {
    Screen screen;
    EqualizerScreenConfig config;
    ListModel model;
    ListView list;
    char text[EQUALIZER_SCREEN_ROWS][EQUALIZER_SCREEN_TEXT_CAPACITY];
    char title[EQUALIZER_SCREEN_TITLE_CAPACITY];
    uint32_t now_ms;
    int slot;
    bool adjusting;
} EqualizerScreen;

Screen *equalizer_screen_init(EqualizerScreen *equalizer, EqualizerScreenConfig config);
