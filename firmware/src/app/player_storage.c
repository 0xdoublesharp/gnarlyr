#include "player_storage.h"

#include <stddef.h>

const char PLAYER_SESSION_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/session.bin";
const char PLAYER_ORDER_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/order.bin";

enum {
    SESSION_RESERVED_BYTES = 4096
};

void player_storage_init(PlayerStorage *storage)
{
    *storage = (PlayerStorage){.session_file.path = PLAYER_SESSION_PATH,
                               .order_file.path = PLAYER_ORDER_PATH};
}

StoreLoad player_storage_load(PlayerStorage *storage, uint32_t fingerprint, uint32_t limit,
                              SessionState *state)
{
    path_file_find(&storage->session_file);
    SessionJournalConfig config = {.file = path_file_store(&storage->session_file),
                                   .fingerprint = fingerprint,
                                   .track_limit = limit};
    return session_journal_open(&storage->journal, config, state);
}

StoreResult player_storage_save(PlayerStorage *storage, uint32_t limit, const SessionState *state)
{
    if (storage->write_blocked ||
        !path_file_prepare(&storage->session_file, SESSION_RESERVED_BYTES)) {
        return STORE_FAILED;
    }
    if (storage->journal.config.file.source.read == NULL || storage->journal.needs_reload ||
        storage->journal.config.fingerprint != state->resume.library_fingerprint) {
        SessionState previous;
        StoreLoad result =
            player_storage_load(storage, state->resume.library_fingerprint, limit, &previous);
        if (result == STORE_UNREADABLE || result == STORE_UNSUPPORTED) {
            return STORE_FAILED;
        }
    }
    return session_journal_save(&storage->journal, state);
}

StoreLoad player_storage_order(PlayerStorage *storage, TrackOrder *order,
                               const uint32_t *generation)
{
    bool ready = generation == NULL
                     ? !storage->write_blocked &&
                           path_file_prepare(&storage->order_file, TRACK_ORDER_FILE_BYTES)
                     : path_file_find(&storage->order_file);
    if (!ready || storage->order_file.size < TRACK_ORDER_FILE_BYTES) {
        return STORE_UNREADABLE;
    }
    TrackOrderConfig config = {.file = path_file_store(&storage->order_file),
                               .fingerprint = storage->journal.config.fingerprint,
                               .track_limit = storage->journal.config.track_limit};
    return generation == NULL ? track_order_open(order, config)
                              : track_order_open_at(order, config, *generation);
}
