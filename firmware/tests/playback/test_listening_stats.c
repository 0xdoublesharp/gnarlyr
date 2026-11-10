#include <string.h>

#include "check.h"
#include "listening_fixture.h"
#include "playback/listening_stats.h"

enum {
    MONDAY = LISTENING_FIXTURE_MONDAY,
    WEDNESDAY = MONDAY + 2,
    NOON = 12 * 3600,
    STEP_LIMIT = 200000,
    WIDE = 64,
    MIXED_PLAYS = 600,
    MIXED_KEYS = 41
};

static ListeningDisk disk;
static ListeningLog log_file;
static ListeningTally tallies[WIDE];

static void reset(void)
{
    listening_disk_reset(&disk);
    CHECK_EQUAL(STORE_EMPTY, listening_log_open(&log_file, listening_disk_config(&disk)));
}

static void add(ListeningPlay play)
{
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
}

static void add_dated(uint32_t day)
{
    add(listening_fixture_dated(0, day, NOON));
}

static ListeningSummary summary_at(uint32_t day, uint32_t second)
{
    ListeningSummaryJob job;
    listening_summary_begin(&job, &log_file, day * LISTENING_FIXTURE_DAY + second);
    for (int step = 0; step < STEP_LIMIT && job.result == STORE_WORKING; step++) {
        listening_summary_step(&job);
    }
    CHECK_EQUAL(STORE_DONE, job.result);
    return job.summary;
}

static void run_top(ListeningTop *job, ListeningGroup group, uint16_t capacity)
{
    CHECK(listening_top_begin(job, &log_file, group, (ListeningScratch){tallies, capacity}));
    for (int step = 0; step < STEP_LIMIT && job->result == STORE_WORKING; step++) {
        listening_top_step(job);
    }
    CHECK_EQUAL(STORE_DONE, job->result);
}

static void without_a_clock_the_summary_is_this_session_and_all_time(void)
{
    reset();
    add(listening_fixture_play(0, 254000));
    add(listening_fixture_dated(1, WEDNESDAY, NOON));
    CHECK_EQUAL(STORE_READY, listening_log_open(&log_file, listening_disk_config(&disk)));
    add(listening_fixture_play(2, 9000));
    disk.reads = 0;
    ListeningSummaryJob job;
    listening_summary_begin(&job, &log_file, 0);
    CHECK_EQUAL(STORE_DONE, listening_summary_step(&job));
    CHECK_EQUAL(0, disk.reads);
    CHECK(!job.summary.clock_known);
    CHECK_EQUAL(3, job.summary.all_time.plays);
    CHECK_EQUAL(254000 + 305000 + 9000, job.summary.all_time.listened_ms);
    CHECK_EQUAL(1, job.summary.all_time.skips);
    CHECK_EQUAL(1, job.summary.session.plays);
    CHECK_EQUAL(9000, job.summary.session.listened_ms);
    CHECK_EQUAL(1, job.summary.session.skips);
    CHECK_EQUAL(0, job.summary.today.plays);
    CHECK_EQUAL(0, job.summary.week.plays);
    CHECK_EQUAL(0, job.summary.streak_days);
}

static void with_a_clock_today_and_this_week_count_the_plays_dated_in_them(void)
{
    reset();
    add(listening_fixture_dated(0, MONDAY - 1, NOON));
    add(listening_fixture_dated(1, MONDAY, NOON));
    add(listening_fixture_dated(2, MONDAY + 1, NOON));
    add(listening_fixture_dated(3, WEDNESDAY, 60));
    ListeningPlay skipped = listening_fixture_dated(4, WEDNESDAY, 3600);
    skipped.listened_ms = 4000;
    skipped.end = LISTENING_SKIPPED;
    add(skipped);
    add(listening_fixture_play(0, 254000));
    add(listening_fixture_dated(0, WEDNESDAY + 1, NOON));
    ListeningSummary summary = summary_at(WEDNESDAY, NOON);
    CHECK(summary.clock_known);
    CHECK_EQUAL(2, summary.today.plays);
    CHECK_EQUAL(305000 + 4000, summary.today.listened_ms);
    CHECK_EQUAL(1, summary.today.skips);
    CHECK_EQUAL(4, summary.week.plays);
    CHECK_EQUAL(305000 + 328000 + 305000 + 4000, summary.week.listened_ms);
    CHECK_EQUAL(1, summary.week.skips);
    CHECK_EQUAL(7, summary.all_time.plays);
    CHECK_EQUAL(7, summary.session.plays);
    CHECK_EQUAL(500, listening_skip_permille(&summary.today));
}

static void the_week_starts_on_monday(void)
{
    reset();
    add(listening_fixture_dated(0, MONDAY - 1, LISTENING_FIXTURE_DAY - 1));
    add(listening_fixture_dated(1, MONDAY, 0));
    CHECK_EQUAL(1, summary_at(MONDAY, 10).week.plays);
    CHECK_EQUAL(1, summary_at(MONDAY + 6, LISTENING_FIXTURE_DAY - 1).week.plays);
    CHECK_EQUAL(0, summary_at(MONDAY + 7, 0).week.plays);
    CHECK_EQUAL(1, summary_at(MONDAY - 1, LISTENING_FIXTURE_DAY - 1).week.plays);
}

static void the_streak_counts_days_in_a_row_up_to_today(void)
{
    reset();
    add_dated(WEDNESDAY - 4);
    add_dated(WEDNESDAY - 2);
    add_dated(WEDNESDAY - 2);
    add_dated(WEDNESDAY - 1);
    add_dated(WEDNESDAY);
    CHECK_EQUAL(3, summary_at(WEDNESDAY, NOON).streak_days);
}

static void a_day_without_a_play_yet_keeps_the_streak_of_the_day_before(void)
{
    reset();
    add_dated(WEDNESDAY - 2);
    add_dated(WEDNESDAY - 1);
    CHECK_EQUAL(2, summary_at(WEDNESDAY, 5).streak_days);
    CHECK_EQUAL(0, summary_at(WEDNESDAY + 1, 5).streak_days);
    CHECK_EQUAL(2, summary_at(WEDNESDAY - 1, NOON).streak_days);
}

static void undated_and_later_plays_do_not_make_a_streak(void)
{
    reset();
    add(listening_fixture_play(0, 254000));
    add_dated(WEDNESDAY + 1);
    add_dated(WEDNESDAY + 2);
    CHECK_EQUAL(0, summary_at(WEDNESDAY, NOON).streak_days);
}

static void a_streak_longer_than_64_days_takes_another_pass(void)
{
    static const uint16_t LENGTHS[] = {63, 64, 65, 128, 150};
    for (int length = 0; length < 5; length++) {
        reset();
        add_dated(WEDNESDAY - LENGTHS[length] - 1);
        for (uint32_t back = LENGTHS[length]; back > 0; back--) {
            add_dated(WEDNESDAY - back + 1);
        }
        disk.reads = 0;
        CHECK_EQUAL(LENGTHS[length], summary_at(WEDNESDAY, NOON).streak_days);
        int sectors = (LENGTHS[length] + 1 + 1 + 7) / LISTENING_SECTOR_RECORDS;
        CHECK_EQUAL(sectors * (1 + LENGTHS[length] / LISTENING_STREAK_WINDOW_DAYS), disk.reads);
    }
}

static void add_hand_made_plays(void)
{
    add(listening_fixture_play(0, 254000));
    add(listening_fixture_play(2, 328000));
    add(listening_fixture_play(0, 254000));
    add(listening_fixture_play(1, 305000));
    add(listening_fixture_play(4, 5000));
    add(listening_fixture_play(3, 305000));
    add(listening_fixture_play(2, 328000));
    add(listening_fixture_play(LISTENING_FIXTURE_UNTITLED, 0));
    add(listening_fixture_play(0, 254000));
    add(listening_fixture_play(1, 100000));
}

static void check_ranked(const ListeningRanked *ranked, uint32_t key, uint32_t listened_ms)
{
    CHECK_EQUAL(key, ranked->tally.key);
    CHECK_EQUAL(listened_ms, ranked->tally.listened_ms);
}

static void top_tracks_are_ranked_by_time_listened(void)
{
    reset();
    add_hand_made_plays();
    ListeningTop job;
    run_top(&job, LISTENING_BY_TRACK, WIDE);
    CHECK_EQUAL(5, job.count);
    check_ranked(&job.top[0], 0x1001, 762000);
    check_ranked(&job.top[1], 0x1003, 656000);
    check_ranked(&job.top[2], 0x1002, 405000);
    check_ranked(&job.top[3], 0x2001, 305000);
    check_ranked(&job.top[4], 0x2002, 5000);
    CHECK_EQUAL(3, job.top[0].tally.listens);
    CHECK_EQUAL(1, job.top[2].tally.listens);
    CHECK_EQUAL(0, job.top[4].tally.listens);
    CHECK_EQUAL(1, job.passes);
}

static void each_ranked_entry_names_a_play_for_the_lookup(void)
{
    reset();
    add_hand_made_plays();
    ListeningTop job;
    run_top(&job, LISTENING_BY_TRACK, WIDE);
    CHECK_EQUAL(0x1001, job.top[0].track.key);
    CHECK_EQUAL(10, job.top[0].track.index);
    CHECK_EQUAL(0xA1, job.top[0].track.album);
    CHECK_EQUAL(0xB1, job.top[0].track.artist);
    CHECK_EQUAL(31, job.top[4].track.index);
    run_top(&job, LISTENING_BY_ALBUM, WIDE);
    CHECK_EQUAL(0x1002, job.top[0].track.key);
    CHECK_EQUAL(11, job.top[0].track.index);
    ListeningText text = {0};
    ListeningLookup lookup = listening_fixture_lookup();
    CHECK(lookup.text(lookup.context, &job.top[0].track, &text));
    CHECK(strcmp("Homogenic", text.album) == 0);
}

static void the_most_replayed_track_has_the_most_listens(void)
{
    reset();
    add_hand_made_plays();
    add(listening_fixture_play(4, 1000));
    add(listening_fixture_play(4, 1000));
    add(listening_fixture_play(4, 1000));
    add(listening_fixture_play(4, 1000));
    ListeningTop job;
    run_top(&job, LISTENING_BY_TRACK, WIDE);
    CHECK_EQUAL(0x1001, job.replayed.tally.key);
    CHECK_EQUAL(3, job.replayed.tally.listens);
    CHECK_EQUAL(762000, job.replayed.tally.listened_ms);
    CHECK_EQUAL(10, job.replayed.track.index);
    add(listening_fixture_play(2, 328000));
    add(listening_fixture_play(2, 328000));
    run_top(&job, LISTENING_BY_TRACK, WIDE);
    CHECK_EQUAL(0x1003, job.replayed.tally.key);
    CHECK_EQUAL(4, job.replayed.tally.listens);
}

static void top_albums_and_artists_add_up_their_tracks(void)
{
    reset();
    add_hand_made_plays();
    ListeningTop job;
    run_top(&job, LISTENING_BY_ALBUM, WIDE);
    CHECK_EQUAL(3, job.count);
    check_ranked(&job.top[0], 0xA1, 762000 + 405000);
    check_ranked(&job.top[1], 0xA2, 656000);
    check_ranked(&job.top[2], 0xA3, 310000);
    CHECK_EQUAL(4, job.top[0].tally.listens);
    run_top(&job, LISTENING_BY_ARTIST, WIDE);
    CHECK_EQUAL(2, job.count);
    check_ranked(&job.top[0], 0xB1, 762000 + 405000 + 656000);
    check_ranked(&job.top[1], 0xB2, 310000);
    CHECK_EQUAL(6, job.top[0].tally.listens);
}

static void equal_times_rank_by_key_and_plays_without_time_are_left_out(void)
{
    reset();
    add(listening_fixture_play(3, 305000));
    add(listening_fixture_play(1, 305000));
    add(listening_fixture_play(LISTENING_FIXTURE_UNTITLED, 0));
    ListeningTop job;
    run_top(&job, LISTENING_BY_TRACK, WIDE);
    CHECK_EQUAL(2, job.count);
    check_ranked(&job.top[0], 0x1002, 305000);
    check_ranked(&job.top[1], 0x2001, 305000);
    CHECK_EQUAL(0x1002, job.replayed.tally.key);
}

static void an_empty_log_ranks_nothing(void)
{
    reset();
    ListeningTop job;
    run_top(&job, LISTENING_BY_TRACK, WIDE);
    CHECK_EQUAL(0, job.count);
    CHECK_EQUAL(0, job.replayed.tally.key);
    CHECK_EQUAL(0, summary_at(WEDNESDAY, NOON).today.plays);
}

static void a_job_without_a_table_is_refused(void)
{
    reset();
    ListeningTop job;
    CHECK(!listening_top_begin(&job, &log_file, LISTENING_BY_TRACK, (ListeningScratch){0}));
    CHECK_EQUAL(STORE_FAILED, listening_top_step(&job));
    CHECK(
        !listening_top_begin(&job, &log_file, LISTENING_BY_TRACK, (ListeningScratch){tallies, 0}));
}

typedef struct Expected {
    uint32_t key;
    uint32_t listened_ms;
    uint16_t listens;
} Expected;

static Expected expected[MIXED_KEYS];

static ListeningPlay mixed_play(uint32_t index)
{
    uint32_t pick = index * 2654435761u >> 8;
    uint32_t song = pick % 7 == 0 ? pick % MIXED_KEYS : pick % 9;
    uint32_t length = 100000 + 1000 * song;
    return (ListeningPlay){.track = {.key = (song + 1) * 0x01010101u,
                                     .album = song % 5,
                                     .artist = song % 3,
                                     .index = (uint16_t)song},
                           .length_ms = length,
                           .listened_ms = pick % 4 == 0 ? pick % 30000 : length,
                           .end = pick % 4 == 0 ? LISTENING_SKIPPED : LISTENING_FINISHED,
                           .output = LISTENING_OUTPUT_BALANCED,
                           .format = LISTENING_FORMAT_HIRES};
}

static void expect(const ListeningPlay *play)
{
    Expected *entry = &expected[play->track.index];
    entry->key = play->track.key;
    entry->listened_ms += play->listened_ms;
    entry->listens = (uint16_t)(entry->listens + (listening_play_listened(play) ? 1 : 0));
}

static bool ahead(const Expected *first, const Expected *second)
{
    return first->listened_ms != second->listened_ms ? first->listened_ms > second->listened_ms
                                                     : first->key < second->key;
}

static const Expected *expected_rank(int rank)
{
    for (int index = 0; index < MIXED_KEYS; index++) {
        int better = 0;
        for (int other = 0; other < MIXED_KEYS; other++) {
            better += ahead(&expected[other], &expected[index]) ? 1 : 0;
        }
        if (better == rank) {
            return &expected[index];
        }
    }
    return NULL;
}

static void add_mixed_plays(void)
{
    memset(expected, 0, sizeof expected);
    for (uint32_t index = 0; index < MIXED_PLAYS; index++) {
        ListeningPlay play = mixed_play(index);
        add(play);
        expect(&play);
    }
}

static void a_small_table_takes_more_passes_and_ranks_the_same(void)
{
    static const uint16_t CAPACITIES[] = {WIDE, 16, 3, 1};
    reset();
    add_mixed_plays();
    ListeningTop wide;
    run_top(&wide, LISTENING_BY_TRACK, WIDE);
    CHECK_EQUAL(1, wide.passes);
    CHECK_EQUAL(LISTENING_TOP_COUNT, wide.count);
    for (int rank = 0; rank < LISTENING_TOP_COUNT; rank++) {
        const Expected *entry = expected_rank(rank);
        check_ranked(&wide.top[rank], entry->key, entry->listened_ms);
        CHECK_EQUAL(entry->listens, wide.top[rank].tally.listens);
        CHECK_EQUAL(entry->key, wide.top[rank].track.key);
    }
    for (int index = 1; index < 4; index++) {
        ListeningTop narrow;
        run_top(&narrow, LISTENING_BY_TRACK, CAPACITIES[index]);
        CHECK(narrow.passes > 1);
        CHECK_EQUAL(wide.count, narrow.count);
        for (int rank = 0; rank < LISTENING_TOP_COUNT; rank++) {
            check_ranked(&narrow.top[rank], wide.top[rank].tally.key,
                         wide.top[rank].tally.listened_ms);
            CHECK_EQUAL(wide.top[rank].tally.listens, narrow.top[rank].tally.listens);
            CHECK_EQUAL(wide.top[rank].track.index, narrow.top[rank].track.index);
        }
        CHECK_EQUAL(wide.replayed.tally.key, narrow.replayed.tally.key);
        CHECK_EQUAL(wide.replayed.tally.listens, narrow.replayed.tally.listens);
    }
}

static void each_step_of_a_ranking_reads_at_most_one_sector(void)
{
    reset();
    add_mixed_plays();
    ListeningTop job;
    CHECK(
        listening_top_begin(&job, &log_file, LISTENING_BY_ARTIST, (ListeningScratch){tallies, 1}));
    for (int step = 0; step < STEP_LIMIT && job.result == STORE_WORKING; step++) {
        int reads = disk.reads;
        listening_top_step(&job);
        CHECK(disk.reads - reads <= 1);
    }
    CHECK_EQUAL(STORE_DONE, job.result);
    CHECK_EQUAL(2, job.count);
    CHECK_EQUAL(2, job.passes);
}

static void a_damaged_record_is_left_out_of_the_statistics(void)
{
    reset();
    add_hand_made_plays();
    disk.bytes[LISTENING_RING_OFFSET + 3 * LISTENING_RECORD_BYTES + 20] ^= 1;
    ListeningTop job;
    run_top(&job, LISTENING_BY_TRACK, WIDE);
    CHECK_EQUAL(1, job.cursor.damaged);
    check_ranked(&job.top[0], 0x1003, 656000);
    check_ranked(&job.top[1], 0x1001, 508000);
    CHECK_EQUAL(10, log_file.root.counters.totals.plays);
}

static void a_torn_last_record_is_left_out_of_the_statistics(void)
{
    reset();
    add_hand_made_plays();
    add(listening_fixture_dated(3, WEDNESDAY, NOON));
    memset(disk.bytes + LISTENING_RING_OFFSET + 11 * LISTENING_RECORD_BYTES + 40, 0, 24);
    ListeningTop job;
    run_top(&job, LISTENING_BY_TRACK, WIDE);
    CHECK_EQUAL(1, job.cursor.damaged);
    check_ranked(&job.top[3], 0x2001, 305000);
    CHECK_EQUAL(0, summary_at(WEDNESDAY, NOON).today.plays);
}

static void the_ranking_covers_exactly_the_plays_still_in_a_full_ring(void)
{
    reset();
    for (uint32_t index = 0; index < LISTENING_LOG_CAPACITY + 10; index++) {
        add(listening_fixture_play(index < 10 ? 3 : (uint8_t)(index % 2), 1000));
    }
    ListeningTop job;
    run_top(&job, LISTENING_BY_TRACK, 1);
    CHECK_EQUAL(2, job.count);
    check_ranked(&job.top[0], 0x1001, LISTENING_LOG_CAPACITY / 2 * 1000);
    check_ranked(&job.top[1], 0x1002, LISTENING_LOG_CAPACITY / 2 * 1000);
    CHECK_EQUAL(2, job.passes);
    CHECK_EQUAL(LISTENING_LOG_CAPACITY + 10, log_file.root.counters.totals.plays);
}

static void a_play_logged_while_a_job_runs_is_not_part_of_it(void)
{
    reset();
    add_hand_made_plays();
    ListeningTop job;
    CHECK(listening_top_begin(&job, &log_file, LISTENING_BY_TRACK, (ListeningScratch){tallies, 2}));
    listening_top_step(&job);
    add(listening_fixture_play(3, 305000));
    add(listening_fixture_play(3, 305000));
    for (int step = 0; step < STEP_LIMIT && job.result == STORE_WORKING; step++) {
        listening_top_step(&job);
    }
    CHECK_EQUAL(STORE_DONE, job.result);
    check_ranked(&job.top[3], 0x2001, 305000);
}

static void an_unreadable_log_fails_the_jobs(void)
{
    reset();
    add_hand_made_plays();
    ListeningTop job;
    CHECK(listening_top_begin(&job, &log_file, LISTENING_BY_TRACK, (ListeningScratch){tallies, 2}));
    disk.refuse_reads = true;
    CHECK_EQUAL(STORE_FAILED, listening_top_step(&job));
    CHECK_EQUAL(STORE_FAILED, listening_top_step(&job));
    ListeningSummaryJob summary;
    listening_summary_begin(&summary, &log_file, WEDNESDAY * LISTENING_FIXTURE_DAY);
    CHECK_EQUAL(STORE_FAILED, listening_summary_step(&summary));
    disk.refuse_reads = false;
}

static void time_listened_to_one_entry_stops_at_the_largest_value(void)
{
    reset();
    add(listening_fixture_play(0, UINT32_MAX - 5));
    add(listening_fixture_play(0, 10));
    ListeningTop job;
    run_top(&job, LISTENING_BY_TRACK, WIDE);
    check_ranked(&job.top[0], 0x1001, UINT32_MAX);
}

static void rank_before_resolution(ListeningTop *job)
{
    reset();
    add_hand_made_plays();
    CHECK(
        listening_top_begin(job, &log_file, LISTENING_BY_TRACK, (ListeningScratch){tallies, WIDE}));
    for (int step = 0; step < STEP_LIMIT && job->sweeping; step++) {
        CHECK_EQUAL(STORE_WORKING, listening_top_step(job));
    }
    CHECK(!job->sweeping);
    CHECK(job->count > 0);
}

static void a_read_failure_while_resolving_a_rank_fails_the_job(void)
{
    ListeningTop job;
    rank_before_resolution(&job);
    disk.refuse_reads = true;
    CHECK_EQUAL(STORE_FAILED, listening_top_step(&job));
    int reads = disk.reads;
    CHECK_EQUAL(STORE_FAILED, listening_top_step(&job));
    CHECK_EQUAL(reads, disk.reads);
    disk.refuse_reads = false;
}

static void a_record_damaged_after_ranking_cannot_resolve_to_another_song(void)
{
    ListeningTop job;
    rank_before_resolution(&job);
    uint32_t sequence = job.cursor.next - job.cursor.kept + job.top[0].tally.position;
    uint32_t offset =
        LISTENING_RING_OFFSET + sequence % LISTENING_LOG_CAPACITY * LISTENING_RECORD_BYTES;
    disk.bytes[offset + 20] ^= 1;
    CHECK_EQUAL(STORE_FAILED, listening_top_step(&job));
    CHECK_EQUAL(0, job.top[0].track.key);
}

static void a_failed_ranking_can_be_restarted_after_storage_recovers(void)
{
    ListeningTop job;
    rank_before_resolution(&job);
    disk.refuse_reads = true;
    CHECK_EQUAL(STORE_FAILED, listening_top_step(&job));
    disk.refuse_reads = false;
    run_top(&job, LISTENING_BY_TRACK, 1);
    CHECK_EQUAL(0x1001, job.top[0].track.key);
    CHECK_EQUAL(762000, job.top[0].tally.listened_ms);
    CHECK_EQUAL(10, log_file.root.counters.totals.plays);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(without_a_clock_the_summary_is_this_session_and_all_time),
        TEST_CASE(with_a_clock_today_and_this_week_count_the_plays_dated_in_them),
        TEST_CASE(the_week_starts_on_monday),
        TEST_CASE(the_streak_counts_days_in_a_row_up_to_today),
        TEST_CASE(a_day_without_a_play_yet_keeps_the_streak_of_the_day_before),
        TEST_CASE(undated_and_later_plays_do_not_make_a_streak),
        TEST_CASE(a_streak_longer_than_64_days_takes_another_pass),
        TEST_CASE(top_tracks_are_ranked_by_time_listened),
        TEST_CASE(each_ranked_entry_names_a_play_for_the_lookup),
        TEST_CASE(the_most_replayed_track_has_the_most_listens),
        TEST_CASE(top_albums_and_artists_add_up_their_tracks),
        TEST_CASE(equal_times_rank_by_key_and_plays_without_time_are_left_out),
        TEST_CASE(an_empty_log_ranks_nothing),
        TEST_CASE(a_job_without_a_table_is_refused),
        TEST_CASE(a_small_table_takes_more_passes_and_ranks_the_same),
        TEST_CASE(each_step_of_a_ranking_reads_at_most_one_sector),
        TEST_CASE(a_damaged_record_is_left_out_of_the_statistics),
        TEST_CASE(a_torn_last_record_is_left_out_of_the_statistics),
        TEST_CASE(the_ranking_covers_exactly_the_plays_still_in_a_full_ring),
        TEST_CASE(a_play_logged_while_a_job_runs_is_not_part_of_it),
        TEST_CASE(an_unreadable_log_fails_the_jobs),
        TEST_CASE(time_listened_to_one_entry_stops_at_the_largest_value),
        TEST_CASE(a_read_failure_while_resolving_a_rank_fails_the_job),
        TEST_CASE(a_record_damaged_after_ranking_cannot_resolve_to_another_song),
        TEST_CASE(a_failed_ranking_can_be_restarted_after_storage_recovers),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
