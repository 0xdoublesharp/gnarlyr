#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/byte_source.h"
#include "playback/resume_state.h"
#include "playback/up_next.h"

enum {
    SESSION_RECORD_BYTES = 512,
    SESSION_FILE_BYTES = 2 * SESSION_RECORD_BYTES
};

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
} SessionState;

typedef struct SessionJournalConfig {
    ByteSource source;
    bool (*write)(void *context, uint32_t offset, const void *bytes, uint32_t length);
    uint32_t size;
    uint32_t fingerprint;
    uint32_t track_limit;
} SessionJournalConfig;

typedef enum SessionLoad {
    SESSION_EMPTY,
    SESSION_READY,
    SESSION_UNREADABLE,
    SESSION_UNSUPPORTED,
    SESSION_CORRUPT
} SessionLoad;

typedef enum SessionSave {
    SESSION_SAVED,
    SESSION_SAVE_FAILED,
    SESSION_SAVE_UNCERTAIN
} SessionSave;

typedef struct SessionJournal {
    SessionJournalConfig config;
    uint32_t generation;
    uint8_t bank;
    bool valid;
    bool needs_reload;
} SessionJournal;

SessionLoad session_journal_open(SessionJournal *journal, SessionJournalConfig config,
                                 SessionState *state);
SessionLoad session_journal_previous(SessionJournal *journal, SessionState *state);
SessionSave session_journal_save(SessionJournal *journal, const SessionState *state);
