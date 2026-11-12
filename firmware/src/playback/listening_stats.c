#include "playback/listening_stats.h"

#include <stddef.h>

enum {
    DAY_SECONDS = 86400,
    MINUTE_SECONDS = 60,
    WEEK_DAYS = 7,
    DAYS_FROM_MONDAY_TO_EPOCH = 3,
    EARLIER = 0x8000u,
    POSITION = EARLIER - 1u
};

_Static_assert((int)LISTENING_LOG_CAPACITY <= (int)EARLIER,
               "a ring position leaves the top bit free");

uint32_t listening_local_seconds(const ListeningPlay *play)
{
    int32_t offset = (int32_t)play->offset_minutes * MINUTE_SECONDS;
    return play->clock_s + (uint32_t)offset;
}

static uint32_t local_day(const ListeningPlay *play)
{
    return listening_local_seconds(play) / DAY_SECONDS;
}

bool listening_span_holds(const ListeningSpan *span, const ListeningPlay *play)
{
    if (!span->dated) {
        return true;
    }
    uint32_t day = local_day(play);
    return play->clock_s != 0 && day >= span->first_day && day <= span->last_day;
}

static uint32_t week_of(uint32_t day)
{
    return (day + DAYS_FROM_MONDAY_TO_EPOCH) / WEEK_DAYS;
}

static void count_periods(ListeningSummaryJob *job, uint32_t day, const ListeningPlay *play)
{
    if (day == job->today) {
        listening_totals_add(&job->summary.today, play);
    }
    if (week_of(day) == week_of(job->today)) {
        listening_totals_add(&job->summary.week, play);
    }
}

static void summarise(void *context, const ListeningRecord *record)
{
    ListeningSummaryJob *job = context;
    uint32_t day = local_day(&record->play);
    if (record->play.clock_s == 0 || day > job->newest) {
        return;
    }
    if (job->newest == job->today) {
        count_periods(job, day, &record->play);
    }
    if (job->newest - day < LISTENING_STREAK_WINDOW_DAYS) {
        job->days |= UINT64_C(1) << (job->newest - day);
    }
}

static bool streak_continues(ListeningSummaryJob *job)
{
    uint8_t day = job->newest == job->today && (job->days & 1) == 0 ? 1 : 0;
    while (day < LISTENING_STREAK_WINDOW_DAYS && (job->days >> day & 1) != 0) {
        job->summary.streak_days++;
        day++;
    }
    return day == LISTENING_STREAK_WINDOW_DAYS && job->newest >= LISTENING_STREAK_WINDOW_DAYS;
}

void listening_summary_begin(ListeningSummaryJob *job, const ListeningLog *log, uint32_t now_s)
{
    *job = (ListeningSummaryJob){.summary = {.all_time = log->root.counters.totals,
                                             .session = log->session,
                                             .clock_known = now_s != 0},
                                 .today = now_s / DAY_SECONDS,
                                 .newest = now_s / DAY_SECONDS,
                                 .result = now_s != 0 ? STORE_WORKING : STORE_DONE};
    listening_cursor_begin(&job->cursor, log, LISTENING_SECTOR_RECORDS);
}

StoreResult listening_summary_step(ListeningSummaryJob *job)
{
    if (job->result != STORE_WORKING) {
        return job->result;
    }
    StoreResult result = listening_cursor_step(&job->cursor, (ListeningVisitor){summarise, job});
    if (result == STORE_DONE && streak_continues(job)) {
        job->newest -= LISTENING_STREAK_WINDOW_DAYS;
        job->days = 0;
        listening_cursor_rewind(&job->cursor);
        result = STORE_WORKING;
    }
    job->result = result;
    return result;
}

static uint32_t group_key(const ListeningTop *job, const ListeningTrack *track)
{
    switch (job->group) {
    case LISTENING_BY_TRACK:
        return track->key;
    case LISTENING_BY_ALBUM:
        return track->album;
    case LISTENING_BY_ARTIST:
        return track->artist;
    }
    return 0;
}

static uint16_t place_of(const ListeningTop *job, uint32_t key)
{
    uint16_t low = 0;
    uint16_t high = job->used;
    while (low < high) {
        uint16_t middle = (uint16_t)((low + high) / 2);
        if (job->scratch.tallies[middle].key < key) {
            low = (uint16_t)(middle + 1);
        } else {
            high = middle;
        }
    }
    return low;
}

static ListeningTally *claim(ListeningTop *job, uint32_t key)
{
    ListeningTally *tallies = job->scratch.tallies;
    uint16_t place = place_of(job, key);
    if (place < job->used && tallies[place].key == key) {
        return &tallies[place];
    }
    bool full = job->used == job->scratch.capacity;
    job->more |= full;
    if (full && place == job->used) {
        return NULL;
    }
    uint16_t last = (uint16_t)(full ? job->used - 1 : job->used);
    for (uint16_t index = last; index > place; index--) {
        tallies[index] = tallies[index - 1];
    }
    tallies[place] = (ListeningTally){.key = key};
    job->used = (uint16_t)(last + 1);
    return &tallies[place];
}

static bool earlier(const ListeningTop *job, const ListeningPlay *play)
{
    return job->count_fresh && job->span.dated && play->clock_s != 0 &&
           local_day(play) < job->span.first_day;
}

static void add_play(const ListeningTop *job, ListeningTally *entry, const ListeningRecord *record)
{
    const ListeningPlay *play = &record->play;
    uint32_t sum = entry->listened_ms + play->listened_ms;
    uint32_t position = record->sequence - (job->cursor.next - job->cursor.kept);
    entry->listened_ms = sum < entry->listened_ms ? UINT32_MAX : sum;
    entry->listens = (uint16_t)(entry->listens + (listening_play_listened(play) ? 1 : 0));
    entry->position = (uint16_t)((entry->position & EARLIER) | (position & POSITION));
}

static void tally(void *context, const ListeningRecord *record)
{
    ListeningTop *job = context;
    const ListeningPlay *play = &record->play;
    bool before = earlier(job, play);
    if (!before && !listening_span_holds(&job->span, play)) {
        return;
    }
    uint32_t key = group_key(job, &play->track);
    ListeningTally *entry = key > job->bound ? claim(job, key) : NULL;
    if (entry != NULL && before) {
        entry->position = (uint16_t)(entry->position | EARLIER);
    } else if (entry != NULL) {
        add_play(job, entry, record);
    }
}

static bool longer(const ListeningTally *first, const ListeningTally *second)
{
    return first->listened_ms != second->listened_ms ? first->listened_ms > second->listened_ms
                                                     : first->key < second->key;
}

static bool replayed_more(const ListeningTally *first, const ListeningTally *second)
{
    return first->listens != second->listens ? first->listens > second->listens
                                             : longer(first, second);
}

static void offer(ListeningTop *job, const ListeningTally *entry)
{
    uint8_t place = job->count;
    while (place > 0 && longer(entry, &job->top[place - 1].tally)) {
        place--;
    }
    if (entry->listened_ms == 0 || place == LISTENING_TOP_COUNT) {
        return;
    }
    uint8_t last = job->count < LISTENING_TOP_COUNT ? job->count : LISTENING_TOP_COUNT - 1;
    for (uint8_t index = last; index > place; index--) {
        job->top[index] = job->top[index - 1];
    }
    job->top[place] = (ListeningRanked){.tally = *entry};
    job->count = (uint8_t)(last + 1);
}

static void rank(ListeningTop *job)
{
    for (uint16_t index = 0; index < job->used; index++) {
        ListeningTally entry = job->scratch.tallies[index];
        bool known_before = (entry.position & EARLIER) != 0;
        entry.position = (uint16_t)(entry.position & POSITION);
        if (entry.listened_ms > 0) {
            job->distinct++;
            job->fresh = (uint16_t)(job->fresh + (known_before ? 0 : 1));
        }
        offer(job, &entry);
        if (entry.listens > 0 && replayed_more(&entry, &job->replayed.tally)) {
            job->replayed = (ListeningRanked){.tally = entry};
        }
    }
}

static StoreResult sweep(ListeningTop *job)
{
    StoreResult result = listening_cursor_step(&job->cursor, (ListeningVisitor){tally, job});
    if (result != STORE_DONE) {
        return result;
    }
    rank(job);
    job->passes++;
    if (job->more) {
        job->bound = job->scratch.tallies[job->used - 1].key;
        job->used = 0;
        job->more = false;
        listening_cursor_rewind(&job->cursor);
    } else {
        job->sweeping = false;
    }
    return STORE_WORKING;
}

static StoreResult resolve(ListeningTop *job)
{
    ListeningRanked *entry = job->resolved < job->count ? &job->top[job->resolved] : &job->replayed;
    ListeningRecord record;
    if (entry->tally.key != 0) {
        if (!listening_cursor_fetch(&job->cursor, entry->tally.position, &record) ||
            group_key(job, &record.play.track) != entry->tally.key) {
            return STORE_FAILED;
        }
        entry->track = record.play.track;
    }
    job->resolved++;
    return job->resolved > job->count ? STORE_DONE : STORE_WORKING;
}

bool listening_top_begin(ListeningTop *job, const ListeningLog *log, ListeningGroup group,
                         ListeningScratch scratch)
{
    bool usable = scratch.tallies != NULL && scratch.capacity > 0;
    *job = (ListeningTop){.scratch = scratch,
                          .group = group,
                          .sweeping = true,
                          .result = usable ? STORE_WORKING : STORE_FAILED};
    listening_cursor_begin(&job->cursor, log, LISTENING_SECTOR_RECORDS);
    return usable;
}

void listening_top_limit(ListeningTop *job, ListeningSpan span, bool count_fresh)
{
    job->span = span;
    job->count_fresh = count_fresh;
}

StoreResult listening_top_step(ListeningTop *job)
{
    if (job->result == STORE_WORKING) {
        job->result = job->sweeping ? sweep(job) : resolve(job);
    }
    return job->result;
}
