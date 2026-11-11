#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/cassette.h"
#include "playback/resume_state.h"
#include "playback/store.h"
#include "playback/up_next.h"

enum {
    SESSION_RECORD_BYTES = STORE_BLOCK_BYTES,
    SESSION_FILE_BYTES = 2 * SESSION_RECORD_BYTES
};

extern const char SESSION_JOURNAL_PATH[];

typedef enum SessionQueueKind {
    SESSION_QUEUE_GENERATED,
    SESSION_QUEUE_STORED,
    SESSION_QUEUE_EMPTY
} SessionQueueKind;

typedef struct SessionState {
    ResumeState resume;
    UpNext up_next;
    uint32_t order_generation;
    SessionQueueKind queue_kind;
    CassetteState cassette;
} SessionState;

typedef struct SessionJournalConfig {
    StoreFile file;
    uint32_t fingerprint;
    uint32_t track_limit;
} SessionJournalConfig;

typedef struct SessionJournal {
    SessionJournalConfig config;
    uint32_t generation;
    uint8_t bank;
    bool valid;
    bool needs_reload;
} SessionJournal;

StoreLoad session_journal_open(SessionJournal *journal, SessionJournalConfig config,
                               SessionState *state);
StoreLoad session_journal_previous(SessionJournal *journal, SessionState *state);
StoreResult session_journal_save(SessionJournal *journal, const SessionState *state);
