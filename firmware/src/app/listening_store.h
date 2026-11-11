#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/listening_export.h"
#include "playback/listening_track.h"
#include "player.h"

enum {
    LISTENING_EXPORT_FILE_BYTES = 2 * 1024 * 1024
};

typedef struct ListeningFile {
    const char *path;
} ListeningFile;

typedef struct ListeningObservation {
    PlayerStatus status;
    uint32_t token;
    ListeningOutput output;
} ListeningObservation;

typedef struct ListeningStore {
    ListeningLog log;
    ListeningFile file;
    const Library *library;
    ListeningPlay active;
    ListeningPlay pending;
    ListeningObservation previous;
    uint32_t pending_sequence;
    uint32_t last_ms;
    uint32_t retry_ms;
    bool observed;
    bool following;
    bool waiting;
    bool failed;
} ListeningStore;

StoreFile listening_store_file(ListeningFile *file, const char *path, uint32_t bytes);
void listening_store_attach(ListeningStore *store, const Library *library, StoreFile file);
void listening_store_load(ListeningStore *store, const Library *library);
void listening_store_follow(ListeningStore *store, ListeningObservation observation,
                            uint32_t now_ms);
void listening_store_flush(ListeningStore *store, ListeningObservation observation,
                           uint32_t now_ms);
ListeningTotals listening_store_totals(const ListeningStore *store);
bool listening_store_text(void *context, const ListeningTrack *track, ListeningText *text);
