#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/listening_log.h"

enum {
    LISTENING_TOP_COUNT = 10
};

typedef enum ListeningGroup {
    LISTENING_BY_TRACK,
    LISTENING_BY_ALBUM,
    LISTENING_BY_ARTIST
} ListeningGroup;

typedef struct ListeningSpan {
    uint32_t first_day;
    uint32_t last_day;
    bool dated;
} ListeningSpan;

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
    ListeningSpan span;
    uint32_t bound;
    ListeningGroup group;
    uint16_t used;
    uint16_t passes;
    uint16_t distinct;
    uint16_t fresh;
    uint8_t count;
    uint8_t resolved;
    bool more;
    bool sweeping;
    bool count_fresh;
    StoreResult result;
} ListeningTop;

bool listening_top_begin(ListeningTop *job, const ListeningLog *log, ListeningGroup group,
                         ListeningScratch scratch);
void listening_top_limit(ListeningTop *job, ListeningSpan span, bool count_fresh);
StoreResult listening_top_step(ListeningTop *job);

uint32_t listening_local_seconds(const ListeningPlay *play);
bool listening_span_holds(const ListeningSpan *span, const ListeningPlay *play);
