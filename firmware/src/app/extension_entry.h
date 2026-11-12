#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/extension.h"
#include "hal/clock.h"
#include "headphone_store.h"
#include "standby.h"
#include "ui/screen.h"
#include "ui/wire_motion.h"

struct AppScreens;

typedef struct ExtensionModuleEntry {
    ExtensionModuleHead head;
    bool (*menu_hold)(struct AppScreens *screens, KeyEvent event);
    void (*standby_follow)(Standby *, PowerPolicy *, const Settings *);
    void (*standby_frame)(Standby *);
    void (*motion_run)(WireMotion *motion, const void *owner, WireShape shape, uint32_t now_ms);
    void (*motion_progress)(WireMotion *motion, uint32_t done, uint32_t total);
    void (*motion_end)(WireMotion *motion, const void *owner);
    bool (*motion_shown)(const WireMotion *motion, const void *owner);
    bool (*motion_due)(const WireMotion *motion, uint32_t now_ms);
    void (*motion_tick)(WireMotion *motion, uint32_t now_ms);
    Rect (*motion_fit)(Rect space);
    void (*motion_draw)(WireMotion *motion, Canvas *canvas, const Theme *theme, Rect space);
    bool (*file_remove)(const char *path);
    void (*forget_time_file)(void);
    void (*headphones_load)(HeadphoneStore *store);
    bool (*clock_set)(HalWallTime time);
    bool (*clock_restore)(int16_t offset_minutes);
} ExtensionModuleEntry;

extern const ExtensionModuleEntry EXTENSION_MODULE_ENTRY;

bool app_menu_hold(struct AppScreens *screens, KeyEvent event);
