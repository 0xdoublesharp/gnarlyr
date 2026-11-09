#pragma once

#include "mixtape_store.h"
#include "playback/mix_resolve.h"
#include "player.h"
#include "ui/list_view.h"
#include "ui/screen.h"
#include "ui/toast.h"

enum {
    MIXTAPE_VISIBLE_ROWS = 8
};

typedef struct MixtapeUiConfig {
    Player *player;
    const Theme *theme;
    Toast *toast;
    Screen *now_playing;
    Screen *up_next;
} MixtapeUiConfig;

typedef struct MixtapeUi {
    Screen screen;
    MixtapeUiConfig config;
    MixtapeStore store;
    MixResolve resolver;
    TrackOrder pending;
    TrackOrderJob writer;
    ListModel model;
    ListView list;
    char labels[MIXTAPE_VISIBLE_ROWS][MIX_NAME_BYTES];
    char name[MIX_NAME_BYTES];
    uint8_t slots[MIX_CATALOG_SLOTS];
    uint32_t now_ms;
    uint16_t slot;
    uint8_t count;
    uint8_t mode;
    uint8_t stage;
    uint8_t character;
    int rows_first;
    bool resume;
    bool borrowed;
    bool loading;
    bool save_queue;
    bool renaming;
} MixtapeUi;

typedef struct MixtapeUiApi {
    Screen *(*init)(MixtapeUi *ui, MixtapeUiConfig config);
    bool (*busy)(const MixtapeUi *ui);
    void (*step)(MixtapeUi *ui, uint32_t now_ms);
    void (*cancel)(MixtapeUi *ui);
} MixtapeUiApi;

const MixtapeUiApi *mixtape_ui_api(void);
