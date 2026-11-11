#pragma once

#include "player.h"
#include "ui/list_view.h"
#include "ui/screen.h"
#include "ui/toast.h"
#include "ui/wire_motion.h"

typedef struct MomentsScreenConfig {
    Player *player;
    const Theme *theme;
    Toast *toast;
    Screen *now_playing;
    WireMotion *motion;
} MomentsScreenConfig;

typedef struct MomentsScreen {
    Screen screen;
    MomentsScreenConfig config;
    ListModel model;
    ListView list;
    uint32_t now_ms;
    uint16_t selected;
    uint8_t mode;
    bool deleting;
    Screen *destination;
} MomentsScreen;

Screen *moments_screen_init(MomentsScreen *screen, MomentsScreenConfig config);
