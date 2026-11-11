#pragma once

#include "listening_store.h"
#include "playback/listening_stats.h"
#include "ui/list_view.h"
#include "ui/screen.h"
#include "ui/wire_motion.h"

typedef struct ListeningScreenConfig {
    ListeningStore *store;
    const Theme *theme;
    WireMotion *motion;
} ListeningScreenConfig;

typedef struct ListeningScreen {
    Screen screen;
    ListeningScreenConfig config;
    ListModel model;
    ListView list;
    uint32_t now_ms;
    uint8_t mode;
    uint8_t selected;
    ListeningGroup group;
} ListeningScreen;

Screen *listening_screen_init(ListeningScreen *screen, ListeningScreenConfig config);
