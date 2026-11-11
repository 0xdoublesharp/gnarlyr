#include <stdio.h>
#include <string.h>

#include "check.h"
#include "scan_guard.h"

static const char TRACK[] = "/Album/01 Track.flac";
static const char SHEET[] = "/Album/Album.cue";

static ScanGuard guard;

static ScanGuard restarted(const ScanGuard *before)
{
    ScanGuard after = {0};
    memcpy(&after, before, SCAN_GUARD_BYTES);
    return after;
}

static void start(uint32_t stops)
{
    guard = restarted(&guard);
    scan_guard_begin(&guard, stops);
}

static void fresh(void)
{
    guard = (ScanGuard){0};
    start(0);
}

static ScanGuardStep open_path(const char *path, bool covers)
{
    return scan_guard_open(&guard, scan_guard_mark(path, 1000, covers));
}

static ScanPhase saved_phase(void)
{
    return (ScanPhase)(guard.mark & SCAN_GUARD_PHASE_MASK);
}

static void track_path(char *path, size_t size, int index)
{
    snprintf(path, size, "/Album/%02d.flac", index);
}

static void a_blank_record_starts_a_first_attempt_without_skips(void)
{
    guard = (ScanGuard){.running = 1, .attempts = 3, .mark = SCAN_PHASE_CUE};
    scan_guard_begin(&guard, 0);
    CHECK_EQUAL(1, guard.attempts);
    CHECK(open_path(TRACK, true) != SCAN_GUARD_SKIP);
    CHECK_EQUAL(0, guard.skips[0]);
}

static void a_started_scan_is_saved_as_its_first_running_attempt(void)
{
    fresh();
    ScanGuard saved = restarted(&guard);
    CHECK_EQUAL(1, saved.running);
    CHECK_EQUAL(1, saved.attempts);
    CHECK(scan_guard_covers_allowed(&saved));
}

static void an_ended_scan_is_no_longer_running(void)
{
    fresh();
    open_path(SHEET, false);
    guard.running = 0;
    start(0);
    CHECK_EQUAL(1, guard.attempts);
    CHECK(open_path(SHEET, false) != SCAN_GUARD_SKIP);
}

static void plain_tracks_are_marked_once_per_batch(void)
{
    char path[32];
    int saves = 0;
    fresh();
    for (int index = 0; index < 2 * SCAN_GUARD_BATCH; index++) {
        track_path(path, sizeof path, index);
        saves += open_path(path, false) == SCAN_GUARD_SAVE;
    }
    CHECK_EQUAL(2, saves);
    CHECK_EQUAL(SCAN_PHASE_BATCH, saved_phase());
}

static void cue_sheets_chapters_playlists_and_covers_are_marked_one_by_one(void)
{
    fresh();
    CHECK_EQUAL(SCAN_GUARD_SAVE, open_path(SHEET, false));
    CHECK_EQUAL(SCAN_PHASE_CUE, saved_phase());
    CHECK_EQUAL(SCAN_GUARD_SAVE, open_path("/Books/Book.m4b", false));
    CHECK_EQUAL(SCAN_PHASE_CHAPTERS, saved_phase());
    CHECK_EQUAL(SCAN_GUARD_SAVE, open_path("/Lists/Mix.m3u", false));
    CHECK_EQUAL(SCAN_PHASE_TAGS, saved_phase());
    CHECK_EQUAL(SCAN_GUARD_SAVE, open_path("/Album/cover.jpg", true));
    CHECK_EQUAL(SCAN_GUARD_SAVE, open_path(TRACK, true));
    CHECK_EQUAL(SCAN_PHASE_COVER, saved_phase());
}

static void a_track_after_an_exact_mark_starts_a_new_batch(void)
{
    fresh();
    CHECK_EQUAL(SCAN_GUARD_SAVE, open_path("/Album/01.flac", false));
    CHECK_EQUAL(SCAN_GUARD_SAVE, open_path(SHEET, false));
    CHECK_EQUAL(SCAN_GUARD_SAVE, open_path("/Album/02.flac", false));
}

static void a_scan_stopped_on_a_cover_skips_that_file_only_for_covers(void)
{
    fresh();
    open_path(TRACK, true);
    start(1);
    CHECK_EQUAL(SCAN_GUARD_SKIP, open_path(TRACK, true));
    CHECK(open_path(TRACK, false) != SCAN_GUARD_SKIP);
}

static void a_scan_stopped_on_a_cue_sheet_skips_it_in_both_passes(void)
{
    fresh();
    open_path(SHEET, false);
    start(1);
    CHECK_EQUAL(SCAN_GUARD_SKIP, open_path(SHEET, false));
    CHECK_EQUAL(SCAN_GUARD_SKIP, open_path(SHEET, true));
}

static void a_skipped_file_with_a_new_size_is_read_again(void)
{
    fresh();
    open_path(SHEET, false);
    start(1);
    CHECK_EQUAL(SCAN_GUARD_SAVE, scan_guard_open(&guard, scan_guard_mark(SHEET, 1001, false)));
}

static void a_scan_stopped_inside_a_batch_marks_that_batch_file_by_file_next_time(void)
{
    char path[32];
    fresh();
    for (int index = 0; index < 3; index++) {
        track_path(path, sizeof path, index);
        open_path(path, false);
    }
    start(1);
    int saves = 0;
    for (int index = 0; index < SCAN_GUARD_BATCH; index++) {
        track_path(path, sizeof path, index);
        saves += open_path(path, false) == SCAN_GUARD_SAVE;
    }
    CHECK_EQUAL(SCAN_GUARD_BATCH, saves);
    CHECK_EQUAL(SCAN_PHASE_TAGS, saved_phase());
    start(2);
    CHECK_EQUAL(SCAN_GUARD_SKIP, open_path(path, false));
    CHECK_EQUAL(SCAN_GUARD_SKIP, open_path(path, true));
}

static void a_track_never_named_is_not_skipped_after_a_batch_stop(void)
{
    fresh();
    open_path("/Album/00.flac", false);
    open_path("/Album/01.flac", false);
    start(1);
    CHECK(open_path("/Album/01.flac", false) != SCAN_GUARD_SKIP);
}

static void covers_are_dropped_once_three_scans_in_a_row_stopped(void)
{
    for (uint32_t stops = 0; stops <= SCAN_GUARD_RETRIES; stops++) {
        fresh();
        start(stops);
        CHECK_EQUAL(stops + 1, guard.attempts);
        CHECK_EQUAL(stops < SCAN_GUARD_RETRIES, scan_guard_covers_allowed(&guard));
    }
}

static void the_skip_list_keeps_the_latest_files(void)
{
    char path[32];
    fresh();
    for (int index = 0; index <= SCAN_GUARD_SKIPS; index++) {
        snprintf(path, sizeof path, "/Album/%d.cue", index);
        open_path(path, false);
        start(1);
    }
    CHECK(open_path("/Album/0.cue", false) != SCAN_GUARD_SKIP);
    for (int index = 1; index <= SCAN_GUARD_SKIPS; index++) {
        snprintf(path, sizeof path, "/Album/%d.cue", index);
        CHECK_EQUAL(SCAN_GUARD_SKIP, open_path(path, false));
    }
}

static void skips_outlast_a_finished_scan(void)
{
    fresh();
    open_path(SHEET, false);
    start(1);
    guard.running = 0;
    start(0);
    CHECK_EQUAL(SCAN_GUARD_SKIP, open_path(SHEET, false));
}

static void a_scan_stopped_before_any_file_skips_nothing(void)
{
    fresh();
    start(1);
    CHECK_EQUAL(2, guard.attempts);
    CHECK_EQUAL(0, guard.careful);
    for (int index = 0; index < SCAN_GUARD_SKIPS; index++) {
        CHECK_EQUAL(0, guard.skips[index]);
    }
}

static void a_skip_reports_the_file_and_the_phase_it_stopped_in(void)
{
    fresh();
    open_path(SHEET, false);
    start(1);
    CHECK_EQUAL(SCAN_GUARD_SKIP, open_path(SHEET, true));
    CHECK_EQUAL(scan_guard_mark(SHEET, 1000, false), guard.seen);
    CHECK_EQUAL(SCAN_PHASE_CUE, guard.seen & SCAN_GUARD_PHASE_MASK);
}

static void marks_differ_by_path_and_size(void)
{
    CHECK(scan_guard_mark(TRACK, 1, false) != scan_guard_mark(TRACK, 2, false));
    CHECK(scan_guard_mark("/a.flac", 1, false) != scan_guard_mark("/b.flac", 1, false));
}

static void the_record_starts_with_its_marker(void)
{
    fresh();
    const uint8_t *bytes = (const uint8_t *)&guard;
    CHECK_EQUAL(0, memcmp(bytes, "DSSG", 4));
    CHECK_EQUAL(1, bytes[SCAN_GUARD_RUNNING_BYTE]);
    CHECK_EQUAL(1, bytes[SCAN_GUARD_ATTEMPTS_BYTE]);
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(a_blank_record_starts_a_first_attempt_without_skips),
        TEST_CASE(a_started_scan_is_saved_as_its_first_running_attempt),
        TEST_CASE(an_ended_scan_is_no_longer_running),
        TEST_CASE(plain_tracks_are_marked_once_per_batch),
        TEST_CASE(cue_sheets_chapters_playlists_and_covers_are_marked_one_by_one),
        TEST_CASE(a_track_after_an_exact_mark_starts_a_new_batch),
        TEST_CASE(a_scan_stopped_on_a_cover_skips_that_file_only_for_covers),
        TEST_CASE(a_scan_stopped_on_a_cue_sheet_skips_it_in_both_passes),
        TEST_CASE(a_skipped_file_with_a_new_size_is_read_again),
        TEST_CASE(a_scan_stopped_inside_a_batch_marks_that_batch_file_by_file_next_time),
        TEST_CASE(a_track_never_named_is_not_skipped_after_a_batch_stop),
        TEST_CASE(covers_are_dropped_once_three_scans_in_a_row_stopped),
        TEST_CASE(the_skip_list_keeps_the_latest_files),
        TEST_CASE(skips_outlast_a_finished_scan),
        TEST_CASE(a_scan_stopped_before_any_file_skips_nothing),
        TEST_CASE(a_skip_reports_the_file_and_the_phase_it_stopped_in),
        TEST_CASE(marks_differ_by_path_and_size),
        TEST_CASE(the_record_starts_with_its_marker),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
