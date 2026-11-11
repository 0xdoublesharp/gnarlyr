#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/extension.h"
#include "headphone_store.h"
#include "home_art.h"
#include "ui/wire_motion.h"

typedef struct ExtensionModuleEntry {
    ExtensionModuleHead head;
    void (*motion_run)(WireMotion *motion, const void *owner, WireShape shape, uint32_t now_ms);
    void (*motion_progress)(WireMotion *motion, uint32_t done, uint32_t total);
    void (*motion_end)(WireMotion *motion, const void *owner);
    bool (*motion_shown)(const WireMotion *motion, const void *owner);
    bool (*motion_due)(const WireMotion *motion, uint32_t now_ms);
    void (*motion_tick)(WireMotion *motion, uint32_t now_ms);
    Rect (*motion_fit)(Rect space);
    void (*motion_draw)(WireMotion *motion, Canvas *canvas, const Theme *theme, Rect space);
    void (*art_init)(HomeArt *art, CoverCache *covers, const LibraryStore *store, Player *player,
                     const Theme *theme);
    void (*art_update)(void *context, uint32_t now_ms);
    void (*art_draw)(void *context, Canvas *canvas, Rect area);
    void (*art_backdrop)(Canvas *canvas, Rect area, const Theme *theme);
    bool (*file_remove)(const char *path);
    void (*forget_time_file)(void);
    void (*headphones_load)(HeadphoneStore *store);
} ExtensionModuleEntry;

extern const ExtensionModuleEntry EXTENSION_MODULE_ENTRY;
