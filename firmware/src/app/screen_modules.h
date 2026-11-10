#pragma once

#include <stdint.h>

#include "ui/screen.h"

struct AppScreens;

typedef enum ScreenModule {
    SCREEN_MODULE_NONE,
    SCREEN_MODULE_VIEWS,
    SCREEN_MODULE_SETTINGS,
    SCREEN_MODULE_COUNT
} ScreenModule;

#define SCREEN_MODULE_MAGIC(module) (0x004D5344u | (uint32_t)(module) << 24)

typedef struct ScreenModuleEntry {
    uint32_t magic;
    void (*bind)(struct AppScreens *screens);
} ScreenModuleEntry;

ScreenModule screen_modules_of(const struct AppScreens *screens, const Screen *screen);
const ScreenModuleEntry *screen_modules_use(struct AppScreens *screens, ScreenModule module);
