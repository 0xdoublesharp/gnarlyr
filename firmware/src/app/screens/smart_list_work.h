#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/favorites.h"
#include "listening_store.h"
#include "order_publish.h"
#include "playback/smart_list.h"
#include "playback/track_keys.h"
#include "screens/smart_list_screen.h"
#include "ui/theme.h"

enum {
    SMART_LIST_WORK_ENTRIES = 1024,
    SMART_LIST_TALLIES = 419,
    SMART_LIST_LABEL_BYTES = 128,
    SMART_LIST_DETAIL_BYTES = 48
};

typedef struct SmartListFavorites {
    SmartFavoriteTrack tracks[LIBRARY_EXTRA_CAPACITY];
    SmartFavoriteAlbum albums[FAVORITES_CAPACITY];
} SmartListFavorites;

typedef struct SmartListRanking {
    SmartList list;
    SmartListFavorites favorites;
    SmartListStamp stamp;
} SmartListRanking;

typedef struct SmartListRows {
    char labels[THEME_MAX_ROWS][SMART_LIST_LABEL_BYTES];
    char details[THEME_MAX_ROWS][SMART_LIST_DETAIL_BYTES];
} SmartListRows;

typedef struct SmartListWork {
    TrackKeys keys;
    PathFile file;
    union {
        struct {
            TrackKeysUpdate job;
            TrackKey entries[SMART_LIST_WORK_ENTRIES];
        } update;
        struct {
            SmartListRanking ranking;
            union {
                SmartTally tallies[SMART_LIST_TALLIES];
                uint16_t albums[FAVORITES_CAPACITY];
            } scratch;
        } rank;
        struct {
            OrderPublish publish;
            SmartListRows rows;
        } ready;
    } phase;
    int first;
    uint8_t stage;
    bool rows_ready;
} SmartListWork;
