#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/listening_log.h"
#include "playback/track_keys.h"

enum {
    SMART_LIST_CAPACITY = 100,
    SMART_LIST_FORGOTTEN_WINDOW = 500
};

typedef enum SmartListKind {
    SMART_LIST_RECENTLY_ADDED,
    SMART_LIST_RECENTLY_PLAYED,
    SMART_LIST_MOST_PLAYED,
    SMART_LIST_NEVER_PLAYED,
    SMART_LIST_FORGOTTEN,
    SMART_LIST_KIND_COUNT
} SmartListKind;

typedef struct SmartFavoriteTrack {
    uint16_t track;
    uint16_t place;
} SmartFavoriteTrack;

typedef struct SmartFavoriteAlbum {
    uint16_t first;
    uint16_t count;
    uint32_t place;
} SmartFavoriteAlbum;

typedef struct SmartFavorites {
    const SmartFavoriteTrack *tracks;
    uint16_t track_count;
    const SmartFavoriteAlbum *albums;
    uint16_t album_count;
} SmartFavorites;

typedef struct SmartTally {
    uint32_t key;
    uint32_t last;
    uint16_t plays;
    uint16_t listens;
} SmartTally;

typedef struct SmartScratch {
    SmartTally *tallies;
    uint16_t capacity;
} SmartScratch;

typedef struct SmartListConfig {
    SmartListKind kind;
    const ListeningLog *log;
    const TrackKeys *keys;
    SmartFavorites favorites;
    SmartScratch scratch;
} SmartListConfig;

typedef struct SmartPick {
    uint32_t primary;
    uint32_t secondary;
    uint16_t track;
} SmartPick;

typedef struct SmartList {
    SmartListConfig config;
    ListeningCursor cursor;
    TrackKey block[TRACK_KEYS_BLOCK_ENTRIES];
    SmartPick picks[SMART_LIST_CAPACITY];
    uint32_t low;
    uint32_t high;
    uint32_t next_entry;
    uint32_t previous_key;
    uint32_t newest;
    uint16_t block_at;
    uint16_t block_held;
    uint16_t used;
    uint16_t matched;
    uint16_t count;
    uint8_t stage;
    uint8_t percent;
    bool more;
    bool has_previous;
    StoreResult result;
} SmartList;

bool smart_list_begin(SmartList *job, SmartListConfig config);
StoreResult smart_list_step(SmartList *job);
uint16_t smart_list_track(const SmartList *job, uint16_t position);
