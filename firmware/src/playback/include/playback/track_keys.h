#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library.h"
#include "playback/store.h"

enum {
    TRACK_KEYS_ENTRY_BYTES = 8,
    TRACK_KEYS_CAPACITY = 65536,
    TRACK_KEYS_BLOCK_ENTRIES = STORE_BLOCK_BYTES / TRACK_KEYS_ENTRY_BYTES,
    TRACK_KEYS_BANK_BYTES = STORE_BLOCK_BYTES + TRACK_KEYS_CAPACITY * TRACK_KEYS_ENTRY_BYTES,
    TRACK_KEYS_RUNS_OFFSET = 2 * TRACK_KEYS_BANK_BYTES,
    TRACK_KEYS_FILE_BYTES = TRACK_KEYS_RUNS_OFFSET + TRACK_KEYS_CAPACITY * TRACK_KEYS_ENTRY_BYTES,
    TRACK_KEYS_MAX_RUNS = 64,
    TRACK_KEYS_LATEST_LIMIT = UINT16_MAX
};

extern const char TRACK_KEYS_PATH[];

typedef struct TrackKey {
    uint32_t key;
    uint16_t track;
    uint16_t arrival;
} TrackKey;

typedef struct TrackKeys {
    StoreFile file;
    uint32_t generation;
    uint32_t binding;
    uint32_t count;
    uint16_t latest;
    uint8_t bank;
    bool valid;
    bool needs_reload;
} TrackKeys;

typedef struct TrackKeysWork {
    TrackKey *entries;
    uint32_t capacity;
} TrackKeysWork;

typedef struct TrackKeysRun {
    uint32_t next;
    uint32_t end;
    uint16_t at;
    uint16_t held;
} TrackKeysRun;

typedef struct TrackKeysUpdate {
    TrackKeys *keys;
    const Library *library;
    TrackKeysWork work;
    StoreWrite write;
    TrackKeysRun runs[TRACK_KEYS_MAX_RUNS];
    TrackKey old[TRACK_KEYS_BLOCK_ENTRIES];
    uint32_t binding;
    uint32_t tracks;
    uint32_t track;
    uint32_t written;
    uint32_t chunk;
    uint32_t old_next;
    uint16_t old_at;
    uint16_t old_held;
    uint16_t slice;
    uint16_t arrival;
    uint8_t run_count;
    uint8_t bank;
    uint8_t stage;
    bool fresh;
    StoreResult result;
} TrackKeysUpdate;

StoreLoad track_keys_open(TrackKeys *keys, StoreFile file);
bool track_keys_current(const TrackKeys *keys, const Library *library);
bool track_keys_read(const TrackKeys *keys, uint32_t first, uint32_t count, TrackKey *entries);

bool track_keys_update_begin(TrackKeysUpdate *job, TrackKeys *keys, const Library *library,
                             TrackKeysWork work);
StoreResult track_keys_update_step(TrackKeysUpdate *job);
void track_keys_update_abort(TrackKeysUpdate *job);
uint8_t track_keys_update_progress(const TrackKeysUpdate *job);
