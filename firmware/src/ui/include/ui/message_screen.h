#pragma once

#include "ui/screen.h"
#include "ui/theme.h"
#include "ui/wire_motion.h"

typedef struct MessageConfig {
    const char *title;
    const char *message;
    const Theme *theme;
    WireMotion *motion;
} MessageConfig;

typedef struct MessageScreen {
    Screen screen;
    MessageConfig config;
} MessageScreen;

Screen *message_screen_init(MessageScreen *message, MessageConfig config);
void message_draw(Canvas *canvas, const Theme *theme, Rect area, const char *text);
void message_draw_working(Canvas *canvas, Rect area, const MessageConfig *config);
