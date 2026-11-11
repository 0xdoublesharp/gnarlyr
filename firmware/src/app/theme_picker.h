#pragma once

#include "theme_runtime.h"
#include "ui/list_view.h"
#include "ui/screen.h"

struct AppScreens;

typedef struct ThemeCatalogEntry {
    char filename[THEME_PACK_FILENAME_BYTES];
    char name[THEME_PACK_NAME_BYTES];
    char author[THEME_PACK_NAME_BYTES];
    char version[32];
    uint8_t id[16];
    bool valid;
    bool verified;
    bool low_contrast;
} ThemeCatalogEntry;

typedef struct ThemeCatalog {
    ThemeCatalogEntry entries[THEME_PACK_MAX_INSTALLED];
    uint8_t count;
} ThemeCatalog;

typedef struct ThemeWork {
    ThemeCatalog catalog;
    ThemePack pack;
    char detail[96];
    bool ready;
} ThemeWork;

ThemePack *theme_catalog_work(void);
char *theme_catalog_detail(void);

typedef struct ThemePicker {
    Screen screen;
    ListView list;
    ListModel model;
    struct AppScreens *screens;
    int preview;
    uint32_t now_ms;
} ThemePicker;

ThemeCatalog *theme_picker_catalog(void);
void theme_catalog_scan(ThemeCatalog *catalog);
bool theme_catalog_same_id(const uint8_t *first, const uint8_t *second);
void theme_picker_init(ThemePicker *picker, struct AppScreens *screens);
