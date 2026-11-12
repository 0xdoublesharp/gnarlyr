#pragma once

#include "playback/store.h"
#include "theme_pack.h"

typedef struct ThemeArtwork {
    ThemePalette palette;
    uint8_t id[16];
    uint8_t base;
    uint8_t asset_count;
    ThemeAssetKey assets[THEME_PACK_MAX_ASSETS];
} ThemeArtwork;

typedef struct ThemeRuntime {
    ThemeArtwork pack;
    Color tile[32];
    uint32_t tile_offset;
    bool tile_valid;
    HalFile file;
    uint8_t saved_id[16];
    uint32_t generation;
    uint8_t bank;
    uint8_t saved_base;
    uint8_t shown_base;
    bool stored;
    bool blocked;
    bool previewing;
    bool active;
} ThemeRuntime;

void theme_runtime_apply(ThemeRuntime *runtime, Theme *theme);
bool theme_runtime_draw(void *context, Canvas *canvas, ThemeArtRequest request);
bool theme_art_module_draw(const Theme *theme, Canvas *canvas, int slot, Rect area);
void theme_runtime_boot(ThemeRuntime *runtime);
void theme_runtime_restore(ThemeRuntime *runtime);
bool theme_runtime_load(ThemeRuntime *runtime, const char *name);
bool theme_runtime_save(ThemeRuntime *runtime, int builtin);
void theme_runtime_close(ThemeRuntime *runtime);
extern const char THEME_PACK_FOLDER[];
extern const char THEME_SELECTION_PATH[];
