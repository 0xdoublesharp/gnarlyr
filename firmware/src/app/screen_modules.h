#pragma once

#include <stdint.h>

#include "ui/screen.h"
#include "ui/theme.h"

struct AppScreens;

typedef enum ScreenModule {
    SCREEN_MODULE_NONE,
    SCREEN_MODULE_VIEWS,
    SCREEN_MODULE_SETTINGS,
    SCREEN_MODULE_LIBRARY,
    SCREEN_MODULE_LISTENING,
    SCREEN_MODULE_EQUALIZER = 9,
    SCREEN_MODULE_PLAYING = 11,
    SCREEN_MODULE_COUNT
} ScreenModule;

#define SCREEN_MODULE_MAGIC(module) (0x004D5344u | (uint32_t)(module) << 24)

typedef struct ScreenModuleEntry {
    uint32_t magic;
    void (*bind)(struct AppScreens *screens);
    void (*art_update)(void *context, uint32_t now_ms);
    void (*art_draw)(void *context, Canvas *canvas, Rect area);
    bool (*theme_draw)(const Theme *theme, Canvas *canvas, int slot, Rect area);
} ScreenModuleEntry;

#define SCREEN_MODULE_ENTRY(number, bind_fn)                                                       \
    {SCREEN_MODULE_MAGIC(number), (bind_fn), home_art_update, home_art_draw, theme_art_module_draw}

void screen_modules_prepare_home(struct AppScreens *screens);
void screen_modules_cancel_previews(struct AppScreens *screens);
ScreenModule screen_modules_of(const struct AppScreens *screens, const Screen *screen);
const ScreenModuleEntry *screen_modules_use(struct AppScreens *screens, ScreenModule module);
