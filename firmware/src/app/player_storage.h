#pragma once

#include "hal/storage.h"
#include "path_file.h"
#include "playback/session_journal.h"
#include "playback/track_order.h"

typedef struct PlayerStorage {
    PathFile session_file;
    PathFile order_file;
    SessionJournal journal;
    TrackOrder order;
} PlayerStorage;

extern const char PLAYER_SESSION_PATH[];
extern const char PLAYER_ORDER_PATH[];

void player_storage_init(PlayerStorage *storage);
StoreLoad player_storage_load(PlayerStorage *storage, uint32_t fingerprint, uint32_t limit,
                              SessionState *state);
StoreResult player_storage_save(PlayerStorage *storage, uint32_t limit, const SessionState *state);
StoreLoad player_storage_order(PlayerStorage *storage, TrackOrder *order,
                               const uint32_t *generation);
