#include <stdio.h>
#include <string.h>
#include <time.h>

#include "check.h"
#include "core/calendar.h"
#include "core/clock_face.h"
#include "listening_fixture.h"
#include "playback/listening_recap.h"

enum {
    DAY = LISTENING_FIXTURE_DAY,
    HOUR = 3600,
    NOON = 12 * HOUR,
    STEP_LIMIT = 400000,
    TALLIES = 512,
    WIDE = 64,
    BIG_TRACKS = 1500,
    BIG_DAYS = 410,
    TRACKS_PER_ALBUM = 4,
    ALBUMS_PER_ARTIST = 3,
    BIG_READ_LIMIT = 6000
};

typedef struct CacheDisk {
    uint8_t bytes[LISTENING_RECAP_FILE_BYTES];
    int reads;
} CacheDisk;

static ListeningDisk disk;
static ListeningLog log_file;
static ListeningTally tallies[TALLIES];
static ListeningRecapJob job;
static CacheDisk cache;

static bool read_cache(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    CacheDisk *file = context;
    file->reads++;
    if (offset > sizeof file->bytes || length > sizeof file->bytes - offset) {
        return false;
    }
    memcpy(bytes, file->bytes + offset, length);
    return true;
}

static bool write_cache(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    CacheDisk *file = context;
    if (offset > sizeof file->bytes || length > sizeof file->bytes - offset) {
        return false;
    }
    memcpy(file->bytes + offset, bytes, length);
    return true;
}

static StoreFile cache_file(void)
{
    return (StoreFile){
        .source = {read_cache, &cache}, .write = write_cache, .size = sizeof cache.bytes};
}

static void reset(void)
{
    listening_disk_reset(&disk);
    memset(&cache, 0, sizeof cache);
    CHECK_EQUAL(STORE_EMPTY, listening_log_open(&log_file, listening_disk_config(&disk)));
}

static void add(ListeningPlay play)
{
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
}

static uint32_t day_of(uint16_t year, uint8_t month, uint8_t day)
{
    return calendar_days((CalendarDate){year, month, day, 0});
}

static void add_on(uint8_t song, uint32_t day, uint32_t second)
{
    add(listening_fixture_dated(song, day, second));
}

static ListeningSpan span_of(ListeningPeriod period, uint32_t local_now_s)
{
    ListeningSpan span;
    CHECK(listening_period_span(period, local_now_s, &span));
    return span;
}

static const ListeningRecap *run_span(ListeningSpan span, uint16_t capacity)
{
    listening_recap_begin(&job, &log_file, span, (ListeningScratch){tallies, capacity});
    for (int step = 0; step < STEP_LIMIT && job.result == STORE_WORKING; step++) {
        listening_recap_step(&job);
    }
    CHECK_EQUAL(STORE_DONE, job.result);
    return &job.recap;
}

static const ListeningRecap *run(ListeningPeriod period, uint32_t local_now_s)
{
    return run_span(span_of(period, local_now_s), WIDE);
}

static void check_entry(const ListeningRecapEntry *entry, uint32_t key, uint32_t listened_ms)
{
    uint32_t keys[] = {entry->track.key, entry->track.album, entry->track.artist};
    CHECK(keys[0] == key || keys[1] == key || keys[2] == key);
    CHECK_EQUAL(listened_ms, entry->listened_ms);
}

static void check_same(const ListeningRecap *first, const ListeningRecap *second)
{
    CHECK_EQUAL(first->span.first_day, second->span.first_day);
    CHECK_EQUAL(first->span.last_day, second->span.last_day);
    CHECK_EQUAL(first->span.dated, second->span.dated);
    CHECK_EQUAL(first->totals.listened_ms, second->totals.listened_ms);
    CHECK_EQUAL(first->totals.plays, second->totals.plays);
    CHECK_EQUAL(first->totals.skips, second->totals.skips);
    for (int index = 0; index < LISTENING_OUTPUT_COUNT; index++) {
        CHECK_EQUAL(first->output_ms[index], second->output_ms[index]);
    }
    for (int index = 0; index < LISTENING_FORMAT_COUNT; index++) {
        CHECK_EQUAL(first->format_ms[index], second->format_ms[index]);
    }
    for (int group = 0; group < LISTENING_RECAP_GROUPS; group++) {
        CHECK_EQUAL(first->counts[group], second->counts[group]);
        for (int index = 0; index < LISTENING_RECAP_TOP; index++) {
            const ListeningRecapEntry *one = &first->top[group][index];
            const ListeningRecapEntry *two = &second->top[group][index];
            CHECK_EQUAL(one->track.key, two->track.key);
            CHECK_EQUAL(one->track.album, two->track.album);
            CHECK_EQUAL(one->track.artist, two->track.artist);
            CHECK_EQUAL(one->track.index, two->track.index);
            CHECK_EQUAL(one->listened_ms, two->listened_ms);
            CHECK_EQUAL(one->listens, two->listens);
        }
    }
    CHECK_EQUAL(first->weekday, second->weekday);
    CHECK_EQUAL(first->hour, second->hour);
    CHECK_EQUAL(first->streak_days, second->streak_days);
    CHECK_EQUAL(first->artists, second->artists);
    CHECK_EQUAL(first->new_artists, second->new_artists);
}

static void each_period_spans_its_local_dates(void)
{
    uint32_t now = day_of(2026, 11, 11) * DAY + 15 * HOUR;
    ListeningSpan month = span_of(LISTENING_THIS_MONTH, now);
    CHECK(month.dated);
    CHECK_EQUAL(day_of(2026, 11, 1), month.first_day);
    CHECK_EQUAL(day_of(2026, 11, 11), month.last_day);
    ListeningSpan last = span_of(LISTENING_LAST_MONTH, now);
    CHECK_EQUAL(day_of(2026, 10, 1), last.first_day);
    CHECK_EQUAL(day_of(2026, 10, 31), last.last_day);
    ListeningSpan year = span_of(LISTENING_THIS_YEAR, now);
    CHECK_EQUAL(day_of(2026, 1, 1), year.first_day);
    CHECK_EQUAL(day_of(2026, 11, 11), year.last_day);
    ListeningSpan all = span_of(LISTENING_ALL_TIME, now);
    CHECK(!all.dated);
    CHECK_EQUAL(day_of(2026, 11, 11), all.last_day);
}

static void last_month_crosses_the_year_and_knows_leap_februaries(void)
{
    ListeningSpan december = span_of(LISTENING_LAST_MONTH, day_of(2027, 1, 5) * DAY);
    CHECK_EQUAL(day_of(2026, 12, 1), december.first_day);
    CHECK_EQUAL(day_of(2026, 12, 31), december.last_day);
    ListeningSpan february = span_of(LISTENING_LAST_MONTH, day_of(2028, 3, 1) * DAY);
    CHECK_EQUAL(day_of(2028, 2, 1), february.first_day);
    CHECK_EQUAL(day_of(2028, 2, 29), february.last_day);
    ListeningSpan january = span_of(LISTENING_THIS_YEAR, day_of(2027, 1, 1) * DAY + 1);
    CHECK_EQUAL(january.first_day, january.last_day);
}

static void without_a_clock_only_all_time_has_a_span(void)
{
    ListeningSpan span;
    CHECK(!listening_period_span(LISTENING_THIS_MONTH, 0, &span));
    CHECK(!listening_period_span(LISTENING_LAST_MONTH, 0, &span));
    CHECK(!listening_period_span(LISTENING_THIS_YEAR, 0, &span));
    CHECK(listening_period_span(LISTENING_ALL_TIME, 0, &span));
    CHECK(!span.dated);
    CHECK_EQUAL(0, span.last_day);
}

static void the_clock_offset_decides_the_month_of_now_and_of_each_play(void)
{
    reset();
    uint32_t midnight = day_of(2026, 11, 1) * DAY;
    ListeningPlay east = listening_fixture_dated(0, 0, midnight - 50 * 60);
    east.offset_minutes = 60;
    ListeningPlay plain = listening_fixture_dated(1, 0, midnight - 2 * HOUR);
    ListeningPlay west = listening_fixture_dated(2, 0, midnight + 30 * 60);
    west.offset_minutes = -60;
    add(east);
    add(plain);
    add(west);
    uint32_t now = clock_local_seconds(midnight - 30 * 60, 60);
    CHECK_EQUAL(day_of(2026, 11, 1), span_of(LISTENING_THIS_MONTH, now).first_day);
    const ListeningRecap *november = run(LISTENING_THIS_MONTH, now);
    CHECK_EQUAL(1, november->totals.plays);
    check_entry(&november->top[2][0], 0x1001, 254000);
    CHECK_EQUAL(0, november->hour);
    const ListeningRecap *october = run(LISTENING_LAST_MONTH, now);
    CHECK_EQUAL(2, october->totals.plays);
    CHECK_EQUAL(23, october->hour);
    uint32_t utc_now = clock_local_seconds(midnight - 30 * 60, 0);
    CHECK_EQUAL(day_of(2026, 10, 1), span_of(LISTENING_THIS_MONTH, utc_now).first_day);
}

static void an_empty_log_gives_an_empty_recap(void)
{
    reset();
    uint32_t now = day_of(2026, 11, 11) * DAY;
    for (int period = 0; period < LISTENING_PERIOD_COUNT; period++) {
        const ListeningRecap *recap = run((ListeningPeriod)period, now);
        CHECK_EQUAL(0, recap->totals.plays);
        CHECK_EQUAL(0, recap->totals.listened_ms);
        for (int group = 0; group < LISTENING_RECAP_GROUPS; group++) {
            CHECK_EQUAL(0, recap->counts[group]);
        }
        CHECK_EQUAL(LISTENING_RECAP_NONE, recap->weekday);
        CHECK_EQUAL(LISTENING_RECAP_NONE, recap->hour);
        CHECK_EQUAL(0, recap->streak_days);
        CHECK_EQUAL(0, recap->artists);
        CHECK_EQUAL(0, recap->new_artists);
    }
}

static void a_recap_counts_only_the_plays_of_its_period(void)
{
    reset();
    uint32_t november = day_of(2026, 11, 3);
    add_on(0, day_of(2026, 10, 20), NOON);
    add_on(3, november, NOON);
    ListeningPlay wireless = listening_fixture_dated(4, november + 1, NOON);
    wireless.output = LISTENING_OUTPUT_BLUETOOTH;
    wireless.format = LISTENING_FORMAT_LOSSY;
    add(wireless);
    ListeningPlay skipped = listening_fixture_dated(1, november + 1, NOON + HOUR);
    skipped.listened_ms = 5000;
    skipped.end = LISTENING_SKIPPED;
    add(skipped);
    const ListeningRecap *recap = run(LISTENING_THIS_MONTH, day_of(2026, 11, 11) * DAY);
    CHECK_EQUAL(3, recap->totals.plays);
    CHECK_EQUAL(1, recap->totals.skips);
    CHECK_EQUAL(305000 + 208000 + 5000, recap->totals.listened_ms);
    CHECK_EQUAL(305000 + 5000, recap->output_ms[LISTENING_OUTPUT_SINGLE_ENDED]);
    CHECK_EQUAL(208000, recap->output_ms[LISTENING_OUTPUT_BLUETOOTH]);
    CHECK_EQUAL(208000, recap->format_ms[LISTENING_FORMAT_LOSSY]);
    CHECK_EQUAL(2, recap->counts[0]);
    check_entry(&recap->top[0][0], 0xB2, 305000 + 208000);
    check_entry(&recap->top[0][1], 0xB1, 5000);
    CHECK_EQUAL(0xA3, recap->top[1][0].track.album);
    check_entry(&recap->top[2][0], 0x2001, 305000);
}

static void all_time_takes_totals_and_the_output_split_from_the_counters(void)
{
    reset();
    add(listening_fixture_play(2, 328000));
    add_on(0, day_of(2026, 10, 20), NOON);
    ListeningPlay wireless = listening_fixture_dated(3, day_of(2026, 11, 2), NOON);
    wireless.output = LISTENING_OUTPUT_USB_DAC;
    add(wireless);
    const ListeningRecap *recap = run(LISTENING_ALL_TIME, day_of(2026, 11, 11) * DAY);
    const ListeningCounters *counters = &log_file.root.counters;
    CHECK_EQUAL(3, recap->totals.plays);
    CHECK_EQUAL(counters->totals.listened_ms, recap->totals.listened_ms);
    CHECK_EQUAL(counters->output_ms[LISTENING_OUTPUT_USB_DAC],
                recap->output_ms[LISTENING_OUTPUT_USB_DAC]);
    CHECK_EQUAL(counters->format_ms[LISTENING_FORMAT_LOSSLESS],
                recap->format_ms[LISTENING_FORMAT_LOSSLESS]);
    check_entry(&recap->top[2][0], 0x1003, 328000);
    CHECK_EQUAL(3, recap->counts[2]);
    CHECK_EQUAL(2, recap->artists);
    CHECK_EQUAL(2, recap->new_artists);
}

static void equal_times_rank_by_key_and_the_first_busiest_day_and_hour_win(void)
{
    reset();
    uint32_t monday = LISTENING_FIXTURE_MONDAY;
    add_on(3, monday + 1, 21 * HOUR);
    add_on(1, monday, 9 * HOUR);
    const ListeningRecap *recap = run(LISTENING_ALL_TIME, (monday + 2) * DAY);
    check_entry(&recap->top[2][0], 0x1002, 305000);
    check_entry(&recap->top[2][1], 0x2001, 305000);
    check_entry(&recap->top[0][0], 0xB1, 305000);
    check_entry(&recap->top[1][0], 0xA1, 305000);
    CHECK_EQUAL(0, recap->weekday);
    CHECK_EQUAL(9, recap->hour);
}

static void the_busiest_weekday_and_hour_follow_time_listened(void)
{
    reset();
    uint32_t monday = LISTENING_FIXTURE_MONDAY;
    add_on(0, monday + 5, 21 * HOUR);
    add_on(0, monday + 12, 21 * HOUR + 600);
    add_on(2, monday + 2, 8 * HOUR);
    const ListeningRecap *recap = run(LISTENING_ALL_TIME, (monday + 13) * DAY);
    CHECK_EQUAL(5, recap->weekday);
    CHECK_EQUAL(21, recap->hour);
}

static void the_top_lists_keep_five_entries_and_skip_unknown_keys(void)
{
    reset();
    uint32_t day = day_of(2026, 11, 2);
    for (uint8_t song = 0; song < LISTENING_FIXTURE_SONGS; song++) {
        ListeningPlay play = listening_fixture_play(song, 100000 + song * 1000u);
        play.clock_s = day * DAY + NOON;
        add(play);
    }
    const ListeningRecap *recap = run(LISTENING_THIS_MONTH, (day + 1) * DAY);
    CHECK_EQUAL(LISTENING_RECAP_TOP, recap->counts[2]);
    check_entry(&recap->top[2][0], 0x3001, 105000);
    check_entry(&recap->top[2][4], 0x1002, 101000);
    CHECK_EQUAL(3, recap->counts[1]);
    CHECK_EQUAL(3, recap->counts[0]);
    CHECK_EQUAL(3, recap->artists);
}

static void the_longest_streak_stays_inside_the_period(void)
{
    reset();
    static const uint8_t DAYS[][2] = {{10, 29}, {10, 30}, {10, 31}, {11, 1}, {11, 2},
                                      {11, 5},  {11, 6},  {11, 7},  {11, 8}, {11, 8}};
    for (size_t index = 0; index < sizeof DAYS / sizeof DAYS[0]; index++) {
        add_on(0, day_of(2026, DAYS[index][0], DAYS[index][1]), NOON);
    }
    uint32_t now = day_of(2026, 11, 11) * DAY;
    CHECK_EQUAL(4, run(LISTENING_THIS_MONTH, now)->streak_days);
    CHECK_EQUAL(3, run(LISTENING_LAST_MONTH, now)->streak_days);
    CHECK_EQUAL(5, run(LISTENING_THIS_YEAR, now)->streak_days);
}

static void all_time_streaks_look_back_1024_days(void)
{
    reset();
    uint32_t today = day_of(2026, 11, 11);
    for (uint32_t back = 1030; back >= 1020; back--) {
        add_on(1, today - back, NOON);
    }
    for (uint32_t back = 5; back >= 1; back--) {
        add_on(1, today - back, NOON);
    }
    add(listening_fixture_play(1, 1000));
    CHECK_EQUAL(5, run(LISTENING_ALL_TIME, today * DAY)->streak_days);
    for (uint32_t back = 900; back >= 894; back--) {
        add_on(1, today - back, NOON);
    }
    CHECK_EQUAL(7, run(LISTENING_ALL_TIME, today * DAY)->streak_days);
    CHECK_EQUAL(0, run(LISTENING_ALL_TIME, 0)->streak_days);
}

static void new_artists_are_those_first_heard_in_the_period(void)
{
    reset();
    add(listening_fixture_play(3, 1000));
    add_on(0, day_of(2026, 10, 12), NOON);
    add_on(1, day_of(2026, 11, 2), NOON);
    add_on(3, day_of(2026, 11, 3), NOON);
    ListeningPlay low = listening_fixture_play(5, 90000);
    low.clock_s = day_of(2026, 11, 4) * DAY + NOON;
    add(low);
    uint32_t now = day_of(2026, 11, 11) * DAY;
    const ListeningRecap *november = run(LISTENING_THIS_MONTH, now);
    CHECK_EQUAL(3, november->artists);
    CHECK_EQUAL(2, november->new_artists);
    const ListeningRecap *october = run(LISTENING_LAST_MONTH, now);
    CHECK_EQUAL(1, october->artists);
    CHECK_EQUAL(1, october->new_artists);
    CHECK_EQUAL(3, run(LISTENING_ALL_TIME, now)->new_artists);
}

static void a_small_table_gives_the_same_recap(void)
{
    reset();
    uint32_t day = day_of(2026, 11, 2);
    for (uint32_t index = 0; index < 60; index++) {
        add_on((uint8_t)(index % 5), day + index % 9, NOON + index);
    }
    add_on(0, day_of(2026, 9, 1), NOON);
    ListeningSpan span = span_of(LISTENING_THIS_MONTH, (day + 9) * DAY);
    ListeningRecap wide = *run_span(span, WIDE);
    ListeningRecap narrow = *run_span(span, 1);
    check_same(&wide, &narrow);
    CHECK_EQUAL(1, narrow.new_artists);
}

static void a_saved_recap_is_used_until_a_play_changes_the_log(void)
{
    reset();
    add_on(0, day_of(2026, 11, 2), NOON);
    add_on(3, day_of(2026, 11, 3), 22 * HOUR);
    uint32_t now = day_of(2026, 11, 11) * DAY;
    StoreFile file = cache_file();
    ListeningRecap computed = *run(LISTENING_THIS_MONTH, now);
    CHECK(listening_recap_save(&file, LISTENING_THIS_MONTH, &job));
    ListeningRecap loaded = {.span = computed.span};
    cache.reads = 0;
    CHECK(listening_recap_load(&file, &log_file, LISTENING_THIS_MONTH, &loaded));
    CHECK_EQUAL(1, cache.reads);
    check_same(&computed, &loaded);
    ListeningRecap other = {.span = span_of(LISTENING_THIS_MONTH, now + DAY)};
    CHECK(!listening_recap_load(&file, &log_file, LISTENING_THIS_MONTH, &other));
    other = (ListeningRecap){.span = computed.span};
    CHECK(!listening_recap_load(&file, &log_file, LISTENING_LAST_MONTH, &other));
    add_on(1, day_of(2026, 11, 4), NOON);
    CHECK(!listening_recap_load(&file, &log_file, LISTENING_THIS_MONTH, &loaded));
}

static void a_damaged_or_older_cache_block_is_not_used(void)
{
    reset();
    add_on(0, day_of(2026, 11, 2), NOON);
    uint32_t now = day_of(2026, 11, 11) * DAY;
    StoreFile file = cache_file();
    run(LISTENING_THIS_YEAR, now);
    CHECK(listening_recap_save(&file, LISTENING_THIS_YEAR, &job));
    cache.bytes[2 * STORE_BLOCK_BYTES + STORE_FIELD_BYTES + 40] ^= 1;
    ListeningRecap loaded = {.span = span_of(LISTENING_THIS_YEAR, now)};
    CHECK(!listening_recap_load(&file, &log_file, LISTENING_THIS_YEAR, &loaded));
    listening_recap_begin(&job, &log_file, loaded.span, (ListeningScratch){tallies, WIDE});
    listening_recap_step(&job);
    add_on(1, day_of(2026, 11, 3), NOON);
    for (int step = 0; step < STEP_LIMIT && job.result == STORE_WORKING; step++) {
        listening_recap_step(&job);
    }
    CHECK(listening_recap_save(&file, LISTENING_THIS_YEAR, &job));
    CHECK(!listening_recap_load(&file, &log_file, LISTENING_THIS_YEAR, &loaded));
}

static void an_unreadable_log_fails_the_recap(void)
{
    reset();
    add_on(0, day_of(2026, 11, 2), NOON);
    listening_recap_begin(&job, &log_file, span_of(LISTENING_ALL_TIME, 0),
                          (ListeningScratch){tallies, WIDE});
    disk.refuse_reads = true;
    CHECK_EQUAL(STORE_FAILED, listening_recap_step(&job));
    CHECK_EQUAL(STORE_FAILED, listening_recap_step(&job));
    disk.refuse_reads = false;
    listening_recap_begin(&job, &log_file, span_of(LISTENING_ALL_TIME, 0),
                          (ListeningScratch){NULL, 0});
    CHECK_EQUAL(STORE_FAILED, listening_recap_step(&job));
}

static ListeningPlay big_play(uint32_t index, uint32_t first_day)
{
    uint32_t track = index * 7919u % BIG_TRACKS;
    uint32_t album = track / TRACKS_PER_ALBUM;
    ListeningPlay play = {.track = {0x10000u + track, 0x20000u + album,
                                    0x30000u + album / ALBUMS_PER_ARTIST, (uint16_t)track},
                          .length_ms = 240000,
                          .listened_ms = 120000 + index % 120000,
                          .end = LISTENING_FINISHED,
                          .output = (ListeningOutput)(index % LISTENING_OUTPUT_COUNT),
                          .format = (ListeningFormat)(index % LISTENING_FORMAT_COUNT),
                          .offset_minutes = 60};
    play.clock_s =
        (first_day + index * BIG_DAYS / LISTENING_LOG_CAPACITY) * DAY + (index * 3797u) % DAY;
    return play;
}

static double seconds_since(struct timespec start)
{
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    return (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

static void a_full_ring_recap_is_measured_and_cached(void)
{
    reset();
    uint32_t today = day_of(2026, 11, 11);
    for (uint32_t index = 0; index < LISTENING_LOG_CAPACITY; index++) {
        add(big_play(index, today - BIG_DAYS));
    }
    static const char *const NAMES[] = {"this month", "last month", "this year", "all time"};
    StoreFile file = cache_file();
    for (int period = 0; period < LISTENING_PERIOD_COUNT; period++) {
        ListeningSpan span = span_of((ListeningPeriod)period, today * DAY);
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        disk.reads = 0;
        listening_recap_begin(&job, &log_file, span, (ListeningScratch){tallies, TALLIES});
        int steps = 0;
        while (steps < STEP_LIMIT && listening_recap_step(&job) == STORE_WORKING) {
            steps++;
        }
        CHECK_EQUAL(STORE_DONE, job.result);
        CHECK(disk.reads < BIG_READ_LIMIT);
        CHECK_EQUAL(LISTENING_RECAP_TOP, job.recap.counts[2]);
        CHECK(listening_recap_save(&file, (ListeningPeriod)period, &job));
        ListeningRecap loaded = {.span = span};
        cache.reads = 0;
        CHECK(listening_recap_load(&file, &log_file, (ListeningPeriod)period, &loaded));
        CHECK_EQUAL(1, cache.reads);
        printf("recap %s: plays=%u steps=%d log_reads=%d host_ms=%.2f cache_reads=%d\n",
               NAMES[period], (unsigned)job.recap.totals.plays, steps, disk.reads,
               seconds_since(start) * 1000.0, cache.reads);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(each_period_spans_its_local_dates),
        TEST_CASE(last_month_crosses_the_year_and_knows_leap_februaries),
        TEST_CASE(without_a_clock_only_all_time_has_a_span),
        TEST_CASE(the_clock_offset_decides_the_month_of_now_and_of_each_play),
        TEST_CASE(an_empty_log_gives_an_empty_recap),
        TEST_CASE(a_recap_counts_only_the_plays_of_its_period),
        TEST_CASE(all_time_takes_totals_and_the_output_split_from_the_counters),
        TEST_CASE(equal_times_rank_by_key_and_the_first_busiest_day_and_hour_win),
        TEST_CASE(the_busiest_weekday_and_hour_follow_time_listened),
        TEST_CASE(the_top_lists_keep_five_entries_and_skip_unknown_keys),
        TEST_CASE(the_longest_streak_stays_inside_the_period),
        TEST_CASE(all_time_streaks_look_back_1024_days),
        TEST_CASE(new_artists_are_those_first_heard_in_the_period),
        TEST_CASE(a_small_table_gives_the_same_recap),
        TEST_CASE(a_saved_recap_is_used_until_a_play_changes_the_log),
        TEST_CASE(a_damaged_or_older_cache_block_is_not_used),
        TEST_CASE(an_unreadable_log_fails_the_recap),
        TEST_CASE(a_full_ring_recap_is_measured_and_cached),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
