#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/listening_stats.h"

typedef enum ListeningPeriod {
    LISTENING_THIS_MONTH,
    LISTENING_LAST_MONTH,
    LISTENING_THIS_YEAR,
    LISTENING_ALL_TIME,
    LISTENING_PERIOD_COUNT
} ListeningPeriod;

enum {
    LISTENING_RECAP_TOP = 5,
    LISTENING_RECAP_GROUPS = 3,
    LISTENING_RECAP_WEEKDAYS = 7,
    LISTENING_RECAP_HOURS = 24,
    LISTENING_RECAP_WINDOW_DAYS = 1024,
    LISTENING_RECAP_NONE = 0xFF,
    LISTENING_RECAP_FILE_BYTES = LISTENING_PERIOD_COUNT * STORE_BLOCK_BYTES
};

extern const char LISTENING_RECAP_PATH[];

typedef struct ListeningRecapEntry {
    ListeningTrack track;
    uint32_t listened_ms;
    uint16_t listens;
} ListeningRecapEntry;

typedef struct ListeningRecap {
    ListeningSpan span;
    ListeningTotals totals;
    uint64_t output_ms[LISTENING_OUTPUT_COUNT];
    uint64_t format_ms[LISTENING_FORMAT_COUNT];
    ListeningRecapEntry top[LISTENING_RECAP_GROUPS][LISTENING_RECAP_TOP];
    uint8_t counts[LISTENING_RECAP_GROUPS];
    uint8_t weekday;
    uint8_t hour;
    uint16_t streak_days;
    uint16_t artists;
    uint16_t new_artists;
} ListeningRecap;

typedef struct ListeningRecapJob {
    const ListeningLog *log;
    ListeningCursor cursor;
    ListeningScratch scratch;
    ListeningTop top;
    ListeningRecap recap;
    uint64_t weekday_ms[LISTENING_RECAP_WEEKDAYS];
    uint64_t hour_ms[LISTENING_RECAP_HOURS];
    uint8_t days[LISTENING_RECAP_WINDOW_DAYS / 8];
    uint8_t stage;
    StoreResult result;
} ListeningRecapJob;

extern const ListeningGroup LISTENING_RECAP_ORDER[LISTENING_RECAP_GROUPS];

bool listening_period_span(ListeningPeriod period, uint32_t local_now_s, ListeningSpan *span);
void listening_recap_begin(ListeningRecapJob *job, const ListeningLog *log, ListeningSpan span,
                           ListeningScratch scratch);
StoreResult listening_recap_step(ListeningRecapJob *job);
bool listening_recap_load(const StoreFile *file, const ListeningLog *log, ListeningPeriod period,
                          ListeningRecap *recap);
bool listening_recap_save(const StoreFile *file, ListeningPeriod period,
                          const ListeningRecapJob *job);
