#pragma once

#include "core/strings.h"
#include "hal/storage.h"
#include "player.h"
#include "ui/screen.h"
#include "ui/theme.h"

typedef struct CardFormatConfig {
    Player *player;
    const Theme *theme;
    void (*notice)(const char *title, const char *text);
    void (*prepare)(void);
    void (*finished)(StringId toast);
} CardFormatConfig;

typedef struct CardFormatScreen {
    Screen screen;
    CardFormatConfig config;
    HalCardInfo card;
    uint32_t shown_ms;
    uint32_t progress_ms;
    uint8_t phase;
    bool erase;
    char message[192];
} CardFormatScreen;

Screen *card_format_screen_init(CardFormatScreen *screen, CardFormatConfig config);
