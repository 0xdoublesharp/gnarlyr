#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/store.h"

enum {
    LISTENING_LOG_CAPACITY = 4096,
    LISTENING_RECORD_BYTES = 64,
    LISTENING_SECTOR_RECORDS = STORE_BLOCK_BYTES / LISTENING_RECORD_BYTES,
    LISTENING_RING_OFFSET = 2 * STORE_BLOCK_BYTES,
    LISTENING_LOG_FILE_BYTES = LISTENING_RING_OFFSET +
        LISTENING_LOG_CAPACITY * LISTENING_RECORD_BYTES
};

extern const char LISTENING_LOG_PATH[];

typedef enum ListeningEnd {
    LISTENING_FINISHED,
    LISTENING_SKIPPED,
    LISTENING_STOPPED,
    LISTENING_REPLACED
} ListeningEnd;

typedef enum ListeningOutput {
    LISTENING_OUTPUT_SINGLE_ENDED,
    LISTENING_OUTPUT_BALANCED,
    LISTENING_OUTPUT_BLUETOOTH,
    LISTENING_OUTPUT_USB_DAC
} ListeningOutput;

typedef enum ListeningFormat {
    LISTENING_FORMAT_LOSSY,
    LISTENING_FORMAT_LOSSLESS,
    LISTENING_FORMAT_HIRES
} ListeningFormat;

enum {
    LISTENING_END_COUNT = LISTENING_REPLACED + 1,
    LISTENING_OUTPUT_COUNT = LISTENING_OUTPUT_USB_DAC + 1,
    LISTENING_FORMAT_COUNT = LISTENING_FORMAT_HIRES + 1
};

typedef enum ListeningEvent {
    LISTENING_BOOT,
    LISTENING_CHARGE_STARTED,
    LISTENING_CHARGE_FULL
} ListeningEvent;

typedef struct ListeningTrack {
    uint32_t key;
    uint32_t album;
    uint32_t artist;
    uint16_t index;
} ListeningTrack;

typedef struct ListeningPlay {
    ListeningTrack track;
    uint32_t length_ms;
    uint32_t listened_ms;
    uint32_t start_ms;
    uint32_t end_ms;
    uint32_t uptime_s;
    uint32_t clock_s;
    int16_t offset_minutes;
    ListeningEnd end;
    ListeningOutput output;
    ListeningFormat format;
} ListeningPlay;

typedef struct ListeningRecord {
    ListeningPlay play;
    uint32_t sequence;
    uint32_t boot;
} ListeningRecord;

typedef struct ListeningTotals {
    uint64_t listened_ms;
    uint32_t plays;
    uint32_t skips;
} ListeningTotals;

typedef struct ListeningCounters {
    ListeningTotals totals;
    uint64_t output_ms[LISTENING_OUTPUT_COUNT];
    uint64_t format_ms[LISTENING_FORMAT_COUNT];
    uint32_t boots;
    uint32_t charge_sessions;
    uint32_t full_charges;
} ListeningCounters;

typedef struct ListeningRoot {
    ListeningCounters counters;
    uint32_t next;
    uint16_t kept;
} ListeningRoot;

typedef struct ListeningLogConfig {
    StoreFile file;
    bool enabled;
} ListeningLogConfig;

typedef struct ListeningLog {
    StoreFile file;
    ListeningRoot root;
    ListeningTotals session;
    uint32_t generation;
    uint8_t bank;
    bool valid;
    bool needs_reload;
    bool enabled;
} ListeningLog;

typedef struct ListeningCursor {
    const StoreFile *file;
    uint32_t next;
    uint16_t kept;
    uint16_t position;
    uint16_t damaged;
    uint8_t run;
} ListeningCursor;

typedef struct ListeningVisitor {
    void (*visit)(void *context, const ListeningRecord *record);
    void *context;
} ListeningVisitor;

StoreLoad listening_log_open(ListeningLog *log, ListeningLogConfig config);
void listening_log_enable(ListeningLog *log, bool enabled);
StoreResult listening_log_append(ListeningLog *log, const ListeningPlay *play);
StoreResult listening_log_clear(ListeningLog *log);
StoreResult listening_log_note(ListeningLog *log, ListeningEvent event);

ListeningFormat listening_format_class(bool lossless, uint32_t sample_rate, uint8_t bits);
bool listening_play_listened(const ListeningPlay *play);
void listening_totals_add(ListeningTotals *totals, const ListeningPlay *play);
uint16_t listening_skip_permille(const ListeningTotals *totals);

void listening_cursor_begin(ListeningCursor *cursor, const ListeningLog *log, uint8_t run);
void listening_cursor_rewind(ListeningCursor *cursor);
StoreResult listening_cursor_step(ListeningCursor *cursor, ListeningVisitor visitor);
bool listening_cursor_fetch(const ListeningCursor *cursor, uint16_t position,
                            ListeningRecord *record);
