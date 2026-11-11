#pragma once

#include "mixtape_ui.h"

typedef struct CassetteFlipScreen {
    Screen screen;
    Player *player;
    const Theme *theme;
    MixtapeUi *mixtapes;
    char name[MIX_NAME_BYTES];
} CassetteFlipScreen;

Screen *cassette_flip_screen_init(CassetteFlipScreen *screen, Player *player, const Theme *theme,
                                  MixtapeUi *mixtapes);

void cassette_flip_screen_enter(void *context, uint32_t now_ms);
