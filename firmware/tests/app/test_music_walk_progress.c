#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "check.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "music_record.h"
#include "music_walk.h"

static uint32_t now;
static uint32_t entries;
static uint32_t frames;
static uint32_t last_frame;
static char last_text[160];
static uint32_t *lent_window;
static uint32_t lent_sectors;
static int returns;

void hal_storage_lend_cache(uint32_t *window, uint32_t sectors)
{
    if (window == NULL) {
        returns++;
        return;
    }
    lent_window = window;
    lent_sectors = sectors;
}
static const char *expected_title = "Music";
static uint32_t recorded_entries;
static MusicFingerprint remembered;

bool music_record_unchanged(MusicFingerprint current)
{
    return current.complete && current.entries == recorded_entries;
}

uint32_t music_record_entries(void)
{
    return recorded_entries;
}

void music_record_remember(MusicFingerprint print)
{
    remembered = print;
}

uint32_t hal_clock_milliseconds(void)
{
    return now;
}

void hal_system_keep_alive(void)
{
}

bool hal_dir_open(HalDir *dir, const char *path)
{
    (void)dir;
    CHECK(path != NULL && strcmp(path, "/") == 0);
    return true;
}

bool hal_dir_next(HalDir *dir, HalDirEntry *entry)
{
    (void)dir;
    if (entries == 600) {
        return false;
    }
    entries++;
    now += 10;
    *entry = (HalDirEntry){0};
    snprintf(entry->name, sizeof entry->name, "%u.mp3", entries);
    return true;
}

void hal_dir_close(HalDir *dir)
{
    (void)dir;
}

void app_show_notice(const char *title, const char *message)
{
    CHECK(strcmp(title, expected_title) == 0);
    CHECK(now - last_frame >= 150);
    last_frame = now;
    frames++;
    snprintf(last_text, sizeof last_text, "%s", message);
    if (now < 5000) {
        CHECK(strstr(message, "elapsed") == NULL);
    }
}

static void the_walk_reports_counts_at_most_every_150_ms_and_elapsed_after_five_seconds(void)
{
    now = entries = frames = last_frame = 0;
    MusicFingerprint print = music_walk_fingerprint(MUSIC_WALK_CHECK);
    CHECK(print.complete);
    CHECK_EQUAL(600, print.entries);
    CHECK_EQUAL(37, frames);
    CHECK(strcmp(last_text, "Checking 1 folders, 592 files\n5 seconds elapsed") == 0);
}

static void a_silent_walk_keeps_the_same_fingerprint_without_drawing(void)
{
    now = entries = frames = last_frame = 0;
    MusicFingerprint print = music_walk_fingerprint(MUSIC_WALK_QUIET);
    CHECK(print.complete);
    CHECK_EQUAL(600, print.entries);
    CHECK_EQUAL(0, frames);
}

static void the_walk_lends_its_own_five_sectors_to_the_storage_cache_and_takes_them_back(void)
{
    now = entries = frames = last_frame = 0;
    returns = 0;
    lent_window = NULL;
    music_walk_fingerprint(MUSIC_WALK_QUIET);
    CHECK(lent_window != NULL);
    CHECK_EQUAL(5, lent_sectors);
    CHECK_EQUAL(1, returns);
}

static void the_check_reports_a_change_only_when_the_walk_differs_from_the_record(void)
{
    now = entries = frames = last_frame = 0;
    recorded_entries = 600;
    CHECK(!music_walk_run(MUSIC_WALK_CHECK));
    now = entries = frames = last_frame = 0;
    recorded_entries = 599;
    CHECK(music_walk_run(MUSIC_WALK_CHECK));
    recorded_entries = 0;
}

static void remembering_without_a_record_expects_a_quarter_more_than_the_scanned_files(void)
{
    now = entries = frames = last_frame = 0;
    expected_title = "Scanning library";
    recorded_entries = 0;
    remembered = (MusicFingerprint){.complete = false};
    CHECK(!music_walk_run(960));
    expected_title = "Music";
    CHECK(remembered.complete);
    CHECK_EQUAL(600, remembered.entries);
    CHECK(strcmp(last_text, "Finishing... 49%") == 0);
}

static void finishing_after_a_scan_shows_the_share_of_the_last_walk(void)
{
    now = entries = frames = last_frame = 0;
    expected_title = "Scanning library";
    MusicFingerprint print = music_walk_fingerprint(1200);
    expected_title = "Music";
    CHECK(print.complete);
    CHECK_EQUAL(37, frames);
    CHECK(strcmp(last_text, "Finishing... 49%") == 0);
}

static void finishing_stops_below_one_hundred_when_the_card_grew(void)
{
    now = entries = frames = last_frame = 0;
    expected_title = "Scanning library";
    music_walk_fingerprint(300);
    expected_title = "Music";
    CHECK(strcmp(last_text, "Finishing... 99%") == 0);
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(the_walk_reports_counts_at_most_every_150_ms_and_elapsed_after_five_seconds),
        TEST_CASE(a_silent_walk_keeps_the_same_fingerprint_without_drawing),
        TEST_CASE(the_walk_lends_its_own_five_sectors_to_the_storage_cache_and_takes_them_back),
        TEST_CASE(the_check_reports_a_change_only_when_the_walk_differs_from_the_record),
        TEST_CASE(remembering_without_a_record_expects_a_quarter_more_than_the_scanned_files),
        TEST_CASE(finishing_after_a_scan_shows_the_share_of_the_last_walk),
        TEST_CASE(finishing_stops_below_one_hundred_when_the_card_grew),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
