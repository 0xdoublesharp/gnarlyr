#include "playback/listening_recap.h"

#include <stddef.h>

#include "core/calendar.h"

enum {
    DAY_SECONDS = 86400,
    HOUR_SECONDS = 3600,
    DAYS_FROM_MONDAY_TO_EPOCH = 3,
    BYTE_BITS = 8,
    STAGE_SUMMARY = 0,
    STAGE_DONE = STAGE_SUMMARY + 1 + LISTENING_RECAP_GROUPS,
    RECAP_BYTES = sizeof(ListeningRecap)
};

_Static_assert((int)RECAP_BYTES <= (int)STORE_INLINE_BYTES, "a recap fits in one block");

const char LISTENING_RECAP_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/recap.bin";

const ListeningGroup LISTENING_RECAP_ORDER[LISTENING_RECAP_GROUPS] = {
    LISTENING_BY_ARTIST, LISTENING_BY_ALBUM, LISTENING_BY_TRACK};

static const StoreKind KIND = {.magic = {'D', 'S', 'R', 'C'}, .version = 1, .inline_payload = true};

bool listening_period_span(ListeningPeriod period, uint32_t local_now_s, ListeningSpan *span)
{
    uint32_t today = local_now_s / DAY_SECONDS;
    *span =
        (ListeningSpan){.first_day = 0, .last_day = today, .dated = period != LISTENING_ALL_TIME};
    if (period == LISTENING_ALL_TIME || local_now_s == 0) {
        return period == LISTENING_ALL_TIME;
    }
    CalendarDate date = calendar_date(today);
    CalendarMonth month = {date.year, date.month};
    if (period == LISTENING_LAST_MONTH) {
        month = month.month == 1 ? (CalendarMonth){(uint16_t)(month.year - 1), CALENDAR_MONTHS}
                                 : (CalendarMonth){month.year, (uint8_t)(month.month - 1)};
        span->first_day = calendar_days((CalendarDate){month.year, month.month, 1, 0});
        span->last_day = span->first_day + calendar_month_days(month) - 1u;
    } else {
        uint8_t first_month = period == LISTENING_THIS_YEAR ? 1 : month.month;
        span->first_day = calendar_days((CalendarDate){month.year, first_month, 1, 0});
    }
    return true;
}

static void note_time(ListeningRecapJob *job, const ListeningPlay *play)
{
    uint32_t local = listening_local_seconds(play);
    uint32_t day = local / DAY_SECONDS;
    uint32_t last = job->recap.span.last_day;
    job->weekday_ms[(day + DAYS_FROM_MONDAY_TO_EPOCH) % LISTENING_RECAP_WEEKDAYS] +=
        play->listened_ms;
    job->hour_ms[local % DAY_SECONDS / HOUR_SECONDS] += play->listened_ms;
    if (day <= last && last - day < LISTENING_RECAP_WINDOW_DAYS) {
        uint32_t back = last - day;
        job->days[back / BYTE_BITS] =
            (uint8_t)(job->days[back / BYTE_BITS] | 1u << (back % BYTE_BITS));
    }
}

static void summarise(void *context, const ListeningRecord *record)
{
    ListeningRecapJob *job = context;
    const ListeningPlay *play = &record->play;
    ListeningRecap *recap = &job->recap;
    if (!listening_span_holds(&recap->span, play)) {
        return;
    }
    listening_totals_add(&recap->totals, play);
    recap->output_ms[play->output] += play->listened_ms;
    recap->format_ms[play->format] += play->listened_ms;
    if (play->clock_s != 0) {
        note_time(job, play);
    }
}

static uint8_t busiest(const uint64_t *times, uint8_t count)
{
    uint8_t best = LISTENING_RECAP_NONE;
    for (uint8_t index = 0; index < count; index++) {
        if (times[index] > 0 && (best == LISTENING_RECAP_NONE || times[index] > times[best])) {
            best = index;
        }
    }
    return best;
}

static uint16_t longest_streak(const ListeningRecapJob *job)
{
    const ListeningSpan *span = &job->recap.span;
    uint32_t reach = span->last_day - span->first_day + 1u;
    uint32_t days = reach < LISTENING_RECAP_WINDOW_DAYS ? reach : LISTENING_RECAP_WINDOW_DAYS;
    uint16_t run = 0;
    uint16_t longest = 0;
    for (uint32_t back = 0; back < days && span->last_day != 0; back++) {
        bool played = (job->days[back / BYTE_BITS] >> (back % BYTE_BITS) & 1u) != 0;
        run = played ? (uint16_t)(run + 1) : 0;
        longest = run > longest ? run : longest;
    }
    return longest;
}

static void finish_summary(ListeningRecapJob *job)
{
    ListeningRecap *recap = &job->recap;
    recap->weekday = busiest(job->weekday_ms, LISTENING_RECAP_WEEKDAYS);
    recap->hour = busiest(job->hour_ms, LISTENING_RECAP_HOURS);
    recap->streak_days = longest_streak(job);
    if (!recap->span.dated) {
        const ListeningCounters *counters = &job->log->root.counters;
        recap->totals = counters->totals;
        for (int index = 0; index < LISTENING_OUTPUT_COUNT; index++) {
            recap->output_ms[index] = counters->output_ms[index];
        }
        for (int index = 0; index < LISTENING_FORMAT_COUNT; index++) {
            recap->format_ms[index] = counters->format_ms[index];
        }
    }
}

static void begin_group(ListeningRecapJob *job)
{
    ListeningGroup group = LISTENING_RECAP_ORDER[job->stage - 1];
    listening_top_begin(&job->top, job->log, group, job->scratch);
    listening_top_limit(&job->top, job->recap.span, group == LISTENING_BY_ARTIST);
}

static void keep_group(ListeningRecapJob *job)
{
    uint8_t group = (uint8_t)(job->stage - 1);
    const ListeningTop *top = &job->top;
    uint8_t count = top->count < LISTENING_RECAP_TOP ? top->count : LISTENING_RECAP_TOP;
    for (uint8_t index = 0; index < count; index++) {
        const ListeningRanked *ranked = &top->top[index];
        job->recap.top[group][index] =
            (ListeningRecapEntry){ranked->track, ranked->tally.listened_ms, ranked->tally.listens};
    }
    job->recap.counts[group] = count;
    if (LISTENING_RECAP_ORDER[group] == LISTENING_BY_ARTIST) {
        job->recap.artists = top->distinct;
        job->recap.new_artists = top->fresh;
    }
}

void listening_recap_begin(ListeningRecapJob *job, const ListeningLog *log, ListeningSpan span,
                           ListeningScratch scratch)
{
    bool usable = scratch.tallies != NULL && scratch.capacity > 0;
    *job = (ListeningRecapJob){
        .log = log,
        .scratch = scratch,
        .recap = {.span = span, .weekday = LISTENING_RECAP_NONE, .hour = LISTENING_RECAP_NONE},
        .stage = STAGE_SUMMARY,
        .result = usable ? STORE_WORKING : STORE_FAILED};
    listening_cursor_begin(&job->cursor, log, LISTENING_SECTOR_RECORDS);
}

static StoreResult step_summary(ListeningRecapJob *job)
{
    StoreResult result = listening_cursor_step(&job->cursor, (ListeningVisitor){summarise, job});
    if (result == STORE_DONE) {
        finish_summary(job);
        job->stage++;
        begin_group(job);
        result = STORE_WORKING;
    }
    return result;
}

static StoreResult step_group(ListeningRecapJob *job)
{
    StoreResult result = listening_top_step(&job->top);
    if (result != STORE_DONE) {
        return result;
    }
    keep_group(job);
    job->stage++;
    if (job->stage == STAGE_DONE) {
        return STORE_DONE;
    }
    begin_group(job);
    return STORE_WORKING;
}

StoreResult listening_recap_step(ListeningRecapJob *job)
{
    if (job->result == STORE_WORKING) {
        job->result = job->stage == STAGE_SUMMARY ? step_summary(job) : step_group(job);
    }
    return job->result;
}

static void copy_bytes(uint8_t *target, const uint8_t *source, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        target[index] = source[index];
    }
}

static StoreSlot slot_of(const StoreFile *file, ListeningPeriod period)
{
    return (StoreSlot){.file = file, .kind = &KIND, .offset = (uint32_t)period * STORE_BLOCK_BYTES};
}

static bool same_span(const ListeningSpan *first, const ListeningSpan *second)
{
    return first->first_day == second->first_day && first->last_day == second->last_day &&
           first->dated == second->dated;
}

bool listening_recap_load(const StoreFile *file, const ListeningLog *log, ListeningPeriod period,
                          ListeningRecap *recap)
{
    uint8_t block[STORE_BLOCK_BYTES];
    StoreHeader header;
    StoreSlot slot = slot_of(file, period);
    if (store_load(&slot, block, &header) != STORE_READY || header.bytes != RECAP_BYTES ||
        header.binding != RECAP_BYTES || header.generation != log->root.next ||
        header.count != log->root.kept) {
        return false;
    }
    ListeningRecap found;
    copy_bytes((uint8_t *)&found, block + STORE_FIELD_BYTES, RECAP_BYTES);
    if (!same_span(&found.span, &recap->span)) {
        return false;
    }
    *recap = found;
    return true;
}

bool listening_recap_save(const StoreFile *file, ListeningPeriod period,
                          const ListeningRecapJob *job)
{
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    copy_bytes(block + STORE_FIELD_BYTES, (const uint8_t *)&job->recap, RECAP_BYTES);
    StoreHeader header = {.generation = job->cursor.next,
                          .bytes = RECAP_BYTES,
                          .binding = RECAP_BYTES,
                          .count = job->cursor.kept};
    store_encode(&KIND, &header, block);
    StoreSlot slot = slot_of(file, period);
    return store_file_write(slot.file, slot.offset, block, STORE_BLOCK_BYTES);
}
