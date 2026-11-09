#pragma once

#include "device/storage_code.h"
#include "playback/session_journal.h"
#include "playback/track_order.h"

typedef struct StorageCodeApi {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    TrackOrderLoad (*order_open)(TrackOrder *order, TrackOrderConfig config);
    TrackOrderLoad (*order_open_at)(TrackOrder *order, TrackOrderConfig config,
                                    uint32_t generation);
    bool (*order_read)(const TrackOrder *order, uint16_t first, uint16_t count, uint16_t *tracks);
    TrackOrderSource (*order_source)(const TrackOrder *order);
    bool (*order_begin)(TrackOrderJob *job, TrackOrder *order, TrackOrderSource source,
                        TrackOrderEdit edit);
    TrackOrderResult (*order_step)(TrackOrderJob *job);
    void (*order_abort)(TrackOrderJob *job);
    uint8_t (*order_progress)(const TrackOrderJob *job);
    SessionLoad (*session_open)(SessionJournal *journal, SessionJournalConfig config,
                                SessionState *state);
    SessionLoad (*session_previous)(SessionJournal *journal, SessionState *state);
    SessionSave (*session_save)(SessionJournal *journal, const SessionState *state);
} StorageCodeApi;

extern const StorageCodeApi STORAGE_CODE_ENTRY;
