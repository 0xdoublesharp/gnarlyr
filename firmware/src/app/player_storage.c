#include "player_storage.h"

#include <stddef.h>

const char PLAYER_SESSION_PATH[] = "/.echo/session2.bin";
const char PLAYER_ORDER_PATH[] = "/.echo/order2.bin";

enum {
    SESSION_RESERVED_BYTES = 4096
};

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const PlayerFile *source = context;
    HalFile file;
    if (!hal_file_open(&file, source->path)) {
        return false;
    }
    bool read = hal_file_read(file, offset, bytes, length);
    hal_file_close(&file);
    return read;
}

static bool write_file(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    const PlayerFile *file = context;
    return hal_file_write_at(file->path, offset, bytes, length);
}

static bool find_file(PlayerFile *source)
{
    HalFile file;
    source->size = 0;
    if (!hal_file_open(&file, source->path)) {
        return false;
    }
    source->size = file.size;
    hal_file_close(&file);
    return true;
}

static bool prepare_file(PlayerFile *file, uint32_t size)
{
    if (!find_file(file)) {
        HalReserveResult result = hal_file_reserve(file->path, size);
        if (result != HAL_RESERVE_CREATED && result != HAL_RESERVE_EXISTS) {
            return false;
        }
        find_file(file);
    }
    return file->size >= size;
}

void player_storage_init(PlayerStorage *storage)
{
    *storage = (PlayerStorage){.session_file.path = PLAYER_SESSION_PATH,
                               .order_file.path = PLAYER_ORDER_PATH};
}

SessionLoad player_storage_load(PlayerStorage *storage, uint32_t fingerprint, uint32_t limit,
                                SessionState *state)
{
    find_file(&storage->session_file);
    SessionJournalConfig config = {.source = {read_file, &storage->session_file},
                                   .write = write_file,
                                   .size = storage->session_file.size,
                                   .fingerprint = fingerprint,
                                   .track_limit = limit};
    return session_journal_open(&storage->journal, config, state);
}

SessionSave player_storage_save(PlayerStorage *storage, uint32_t limit, const SessionState *state)
{
    if (!prepare_file(&storage->session_file, SESSION_RESERVED_BYTES)) {
        return SESSION_SAVE_FAILED;
    }
    if (storage->journal.config.source.read == NULL || storage->journal.needs_reload ||
        storage->journal.config.fingerprint != state->resume.library_fingerprint) {
        SessionState previous;
        SessionLoad result =
            player_storage_load(storage, state->resume.library_fingerprint, limit, &previous);
        if (result == SESSION_UNREADABLE || result == SESSION_UNSUPPORTED) {
            return SESSION_SAVE_FAILED;
        }
    }
    return session_journal_save(&storage->journal, state);
}

TrackOrderLoad player_storage_order(PlayerStorage *storage, TrackOrder *order,
                                    const uint32_t *generation)
{
    bool ready = generation == NULL ? prepare_file(&storage->order_file, TRACK_ORDER_FILE_BYTES)
                                    : find_file(&storage->order_file);
    if (!ready || storage->order_file.size < TRACK_ORDER_FILE_BYTES) {
        return TRACK_ORDER_UNREADABLE;
    }
    TrackOrderConfig config = {.source = {read_file, &storage->order_file},
                               .write = write_file,
                               .size = storage->order_file.size,
                               .fingerprint = storage->journal.config.fingerprint,
                               .track_limit = storage->journal.config.track_limit};
    return generation == NULL ? track_order_open(order, config)
                              : track_order_open_at(order, config, *generation);
}
