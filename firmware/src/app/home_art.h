#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cover_cache.h"
#include "gfx/canvas.h"
#include "library_store.h"
#include "player.h"
#include "ui/theme.h"

enum {
    HOME_ART_CYCLE_MS = 5000
};

typedef struct HomeArt {
    CoverCache *covers;
    const LibraryStore *store;
    Player *player;
    const Theme *theme;
    bool cycling;
    bool has_album;
    uint16_t album;
    uint32_t next_change_ms;
    uint16_t shown_track;
    uint16_t shown_album;
    bool metadata_known;
    bool metadata_track;
    char title[96];
    char artist[96];
} HomeArt;

void home_art_init(HomeArt *art, CoverCache *covers, const LibraryStore *store, Player *player,
                   const Theme *theme);
void home_art_update(void *context, uint32_t now_ms);
void home_art_draw(void *context, Canvas *canvas, Rect area);

void home_art_draw_backdrop(Canvas *canvas, Rect area, const Theme *theme);
