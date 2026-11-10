#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/listening_log.h"

enum {
    LISTENING_TOP_COUNT = 10,
    LISTENING_STREAK_WINDOW_DAYS = 64
};

typedef struct ListeningSummary {
    ListeningTotals all_time;
    ListeningTotals session;
    ListeningTotals today;
    ListeningTotals week;
    uint16_t streak_days;
    bool clock_known;
} ListeningSummary;

typedef struct ListeningSummaryJob {
    ListeningCursor cursor;
    ListeningSummary summary;
    uint64_t days;
    uint32_t today;
    uint32_t newest;
    StoreResult result;
} ListeningSummaryJob;

typedef enum ListeningGroup {
    LISTENING_BY_TRACK,
    LISTENING_BY_ALBUM,
    LISTENING_BY_ARTIST
} ListeningGroup;

typedef struct ListeningTally {
    uint32_t key;
    uint32_t listened_ms;
    uint16_t listens;
    uint16_t position;
} ListeningTally;

typedef struct ListeningScratch {
    ListeningTally *tallies;
    uint16_t capacity;
} ListeningScratch;

typedef struct ListeningRanked {
    ListeningTally tally;
    ListeningTrack track;
} ListeningRanked;

typedef struct ListeningTop {
    ListeningCursor cursor;
    ListeningScratch scratch;
    ListeningRanked top[LISTENING_TOP_COUNT];
    ListeningRanked replayed;
    uint32_t bound;
    ListeningGroup group;
    uint16_t used;
    uint16_t passes;
    uint8_t count;
    uint8_t resolved;
    bool more;
    bool sweeping;
    StoreResult result;
} ListeningTop;

void listening_summary_begin(ListeningSummaryJob *job, const ListeningLog *log, uint32_t now_s);
StoreResult listening_summary_step(ListeningSummaryJob *job);

bool listening_top_begin(ListeningTop *job, const ListeningLog *log, ListeningGroup group,
                         ListeningScratch scratch);
StoreResult listening_top_step(ListeningTop *job);
