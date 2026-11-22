#pragma once

#include <stdint.h>

#include "ui/list_view.h"
#include "ui/screen.h"
#include "ui/theme.h"

enum {
    CONFIRM_SECTIONS = 2,
    CONFIRM_CHOICES = 3,
    CONFIRM_CANCEL = 0
};

typedef struct ConfirmSection {
    const char *label;
    const char *text;
} ConfirmSection;

typedef struct ConfirmConfig {
    const char *title;
    const char *subject;
    ConfirmSection sections[CONFIRM_SECTIONS];
    const char *choices[CONFIRM_CHOICES];
    uint8_t choice_count;
    uint8_t first_danger;
    ScreenAction (*choose)(void *context, int index);
    void *context;
    const Theme *theme;
} ConfirmConfig;

typedef struct ConfirmScreen {
    Screen screen;
    ConfirmConfig config;
    ListModel model;
    ListView list;
    uint32_t now_ms;
} ConfirmScreen;

Screen *confirm_screen_init(ConfirmScreen *confirm, ConfirmConfig config);
