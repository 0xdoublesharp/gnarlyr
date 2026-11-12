#pragma once

#include <stddef.h>

#include "cover_cache.h"
#include "listening_store.h"
#include "playback/listening_stats.h"
#include "settings/settings.h"
#include "ui/list_view.h"
#include "ui/screen.h"
#include "ui/toast.h"
#include "ui/wire_motion.h"

typedef struct ListeningScreenConfig {
    ListeningStore *store;
    const Theme *theme;
    WireMotion *motion;
    Toast *toast;
    const bool *scanning;
    CoverCache *covers;
    const Settings *settings;
} ListeningScreenConfig;

typedef struct ExportStatus {
    const char *headline;
    const char *detail;
    const char *hint;
    WireMotion *motion;
} ExportStatus;

typedef struct ListeningScreen {
    Screen screen;
    ListeningStore *store;
    const Theme *theme;
    WireMotion *motion;
    Screen *moments;
    ListModel model;
    ListView list;
    uint32_t now_ms;
    uint8_t mode;
    uint8_t selected;
    bool clearing_export;
    uint8_t card;
    ListeningGroup group;
    uint8_t period;
} ListeningScreen;

Screen *listening_screen_init(ListeningScreen *screen, ListeningScreenConfig config);
void listening_screen_format_time(char *text, size_t capacity, uint64_t millis);
void listening_screen_draw_status(const Theme *theme, Canvas *canvas, Rect area,
                                  ExportStatus status);
