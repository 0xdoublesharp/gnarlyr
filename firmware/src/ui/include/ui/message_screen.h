#pragma once

#include <stdint.h>

#include "gfx/canvas.h"
#include "ui/screen.h"
#include "ui/theme.h"

typedef struct MessageConfig {
    const char *title;
    const char *message;
    const Theme *theme;
    uint8_t icon;
} MessageConfig;

typedef struct MessageCard {
    uint8_t icon;
    const char *text;
} MessageCard;

typedef struct MessageScreen {
    Screen screen;
    MessageConfig config;
} MessageScreen;

Screen *message_screen_init(MessageScreen *message, MessageConfig config);
void message_draw_card(Canvas *canvas, const Theme *theme, Rect area, MessageCard card);
