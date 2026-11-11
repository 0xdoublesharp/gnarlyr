#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "browse_screen.h"
#include "listening_store.h"
#include "playback/smart_list.h"
#include "ui/list_view.h"
#include "ui/menu_screen.h"
#include "ui/screen.h"

typedef struct SmartListStamp {
    uint32_t log;
    uint32_t keys;
    uint32_t binding;
    uint32_t favorites;
    uint16_t extra;
} SmartListStamp;

typedef struct SmartListCache {
    SmartListStamp stamp;
    uint16_t count;
    uint16_t tracks[SMART_LIST_CAPACITY];
    bool valid;
} SmartListCache;

typedef struct SmartListScreenConfig {
    ListeningStore *listening;
    BrowsePool *browse;
} SmartListScreenConfig;

struct SmartListScreen;

typedef struct SmartListEntry {
    struct SmartListScreen *screen;
    SmartListKind kind;
} SmartListEntry;

typedef struct SmartListScreen {
    Screen screen;
    SmartListScreenConfig config;
    MenuScreen menu;
    MenuItem items[SMART_LIST_KIND_COUNT];
    SmartListEntry entries[SMART_LIST_KIND_COUNT];
    ListModel model;
    ListView list;
    SmartListCache caches[SMART_LIST_KIND_COUNT];
    uint32_t now_ms;
    SmartListKind kind;
    uint8_t status;
    uint8_t percent;
    bool checking;
} SmartListScreen;

Screen *smart_list_screen_init(SmartListScreen *screen, SmartListScreenConfig config);
