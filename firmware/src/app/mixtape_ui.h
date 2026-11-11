#pragma once

#include "mixtape_store.h"
#include "playback/mix_resolve.h"
#include "player.h"
#include "ui/list_view.h"
#include "ui/screen.h"
#include "ui/toast.h"
#include "ui/wire_motion.h"

enum {
    MIXTAPE_VISIBLE_ROWS = THEME_MAX_ROWS
};

typedef struct MixtapeUiConfig {
    Player *player;
    const Theme *theme;
    Toast *toast;
    Screen *now_playing;
    Screen *up_next;
    WireMotion *motion;
} MixtapeUiConfig;

typedef struct MixtapeUi {
    Screen screen;
    MixtapeUiConfig config;
    MixtapeStore store;
    MixResolve resolver;
    TrackOrder pending;
    TrackOrderJob writer;
    MixtapeFile append_file;
    MixTape append_tape;
    MixTapeCursor append_cursor;
    MixIdentity add_identity;
    ListModel model;
    ListView list;
    char labels[MIXTAPE_VISIBLE_ROWS][MIX_NAME_BYTES];
    char name[MIX_NAME_BYTES];
    uint8_t slots[MIX_CATALOG_SLOTS];
    uint32_t now_ms;
    uint16_t slot;
    uint16_t add_track;
    uint16_t side_a_count;
    uint8_t side_minutes;
    uint8_t selected_side;
    uint8_t count;
    uint8_t mode;
    uint8_t parent_mode;
    uint8_t stage;
    uint8_t character;
    int rows_first;
    bool resume;
    bool borrowed;
    bool loading;
    bool save_queue;
    bool renaming;
    bool adding;
    bool appending;
    bool add_requested;
    Screen *destination;
} MixtapeUi;

typedef struct MixtapeUiApi {
    Screen *(*init)(MixtapeUi *ui, MixtapeUiConfig config);
    bool (*busy)(const MixtapeUi *ui);
    void (*step)(MixtapeUi *ui, uint32_t now_ms);
    void (*cancel)(MixtapeUi *ui);
    Screen *(*add)(MixtapeUi *ui, uint16_t track);
} MixtapeUiApi;

const MixtapeUiApi *mixtape_ui_api(void);
