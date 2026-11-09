#pragma once

#include "hal/storage.h"
#include "playback/session_journal.h"
#include "playback/track_order.h"

typedef struct PlayerFile {
    const char *path;
    uint32_t size;
} PlayerFile;

typedef struct PlayerStorage {
    PlayerFile session_file;
    PlayerFile order_file;
    SessionJournal journal;
    TrackOrder order;
} PlayerStorage;

extern const char PLAYER_SESSION_PATH[];
extern const char PLAYER_ORDER_PATH[];

void player_storage_init(PlayerStorage *storage);
SessionLoad player_storage_load(PlayerStorage *storage, uint32_t fingerprint, uint32_t limit,
                                SessionState *state);
SessionSave player_storage_save(PlayerStorage *storage, uint32_t limit, const SessionState *state);
TrackOrderLoad player_storage_order(PlayerStorage *storage, TrackOrder *order,
                                    const uint32_t *generation);
