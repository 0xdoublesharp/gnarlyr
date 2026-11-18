#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "check.h"
#include "core/drive_path.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "music_check.h"
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
static uint32_t recorded_generation;
static uint32_t generation;
static bool combined;
static int internal_opens;
static int writes;
static MusicRecord remembered;

bool music_record_read(MusicRecord *record)
{
    *record = (MusicRecord){.card = {.complete = true, .entries = recorded_entries},
                            .internal_state = MUSIC_DRIVE_NONE,
                            .generation = recorded_generation};
    return recorded_entries > 0;
}

void music_record_write(const MusicRecord *record)
{
    remembered = *record;
    writes++;
}

bool music_record_same_music(const MusicRecord *stored, const MusicRecord *current,
                             uint32_t internal_songs)
{
    (void)internal_songs;
    return current->card.complete && stored->card.entries == current->card.entries &&
           stored->internal_state == current->internal_state;
}

uint32_t music_check_generation(void)
{
    return generation;
}

static bool firmware_new;
static bool settled = true;

bool music_check_firmware_is_new(void)
{
    return firmware_new;
}

bool music_record_settled(void)
{
    return settled;
}

bool hal_file_combined(void)
{
    return combined;
}

uint32_t hal_clock_milliseconds(void)
{
    return now;
}

void hal_system_keep_alive(void)
{
}

static int deep_levels;
static int depth;
static bool listed[40];

bool hal_dir_open(HalDir *dir, const char *path)
{
    if (deep_levels > 0) {
        dir->handle = path == NULL ? ++depth : 0;
        listed[dir->handle] = false;
        return true;
    }
    (void)dir;
    CHECK(path != NULL);
    internal_opens += strcmp(path, DRIVE_PATH_INTERNAL_ROOT) == 0 ? 1 : 0;
    CHECK(strcmp(path, "/") == 0 || strcmp(path, DRIVE_PATH_INTERNAL_ROOT) == 0);
    return true;
}

bool hal_dir_next(HalDir *dir, HalDirEntry *entry)
{
    if (deep_levels > 0) {
        bool more = !listed[dir->handle] && dir->handle < deep_levels;
        listed[dir->handle] = true;
        *entry = (HalDirEntry){.directory = true, .name = {'d'}};
        return more;
    }
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
    remembered = (MusicRecord){.card = {.complete = false}};
    CHECK(!music_walk_run(960));
    expected_title = "Music";
    CHECK(remembered.card.complete);
    CHECK_EQUAL(600, remembered.card.entries);
    CHECK(strcmp(last_text, "Finishing... 49%") == 0);
}

static void an_unchanged_check_records_a_moved_generation_and_otherwise_writes_nothing(void)
{
    now = entries = frames = last_frame = 0;
    recorded_entries = 600;
    recorded_generation = generation = 3;
    writes = 0;
    CHECK(!music_walk_run(MUSIC_WALK_CHECK));
    CHECK_EQUAL(0, writes);
    now = entries = frames = last_frame = 0;
    generation = 4;
    CHECK(!music_walk_run(MUSIC_WALK_CHECK));
    CHECK_EQUAL(1, writes);
    CHECK_EQUAL(4, remembered.generation);
    recorded_entries = recorded_generation = generation = 0;
}

static void a_combined_library_also_walks_internal_storage_from_its_root(void)
{
    now = entries = frames = last_frame = 0;
    recorded_entries = 0;
    combined = true;
    internal_opens = 0;
    expected_title = "Scanning library";
    CHECK(!music_walk_run(960));
    expected_title = "Music";
    CHECK_EQUAL(1, internal_opens);
    CHECK_EQUAL(MUSIC_DRIVE_KNOWN, remembered.internal_state);
    CHECK(remembered.internal.complete);
    combined = false;
}

static void a_manual_rescan_is_a_change_after_new_firmware_or_an_unfinished_scan(void)
{
    now = entries = frames = last_frame = 0;
    CHECK(!music_walk_run(MUSIC_WALK_MANUAL));
    CHECK_EQUAL(0, (int)entries);
    firmware_new = true;
    CHECK(music_walk_run(MUSIC_WALK_MANUAL));
    firmware_new = false;
    settled = false;
    CHECK(music_walk_run(MUSIC_WALK_MANUAL));
    settled = true;
}

static MusicFingerprint deep_walk(int levels)
{
    now = entries = frames = last_frame = 0;
    deep_levels = levels;
    depth = 0;
    MusicFingerprint print = music_walk_fingerprint(MUSIC_WALK_QUIET);
    deep_levels = 0;
    return print;
}

static void folders_deeper_than_the_open_folder_slots_leave_the_walk_incomplete(void)
{
    CHECK(deep_walk(10).complete);
    CHECK(!deep_walk(20).complete);
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
        TEST_CASE(an_unchanged_check_records_a_moved_generation_and_otherwise_writes_nothing),
        TEST_CASE(a_combined_library_also_walks_internal_storage_from_its_root),
        TEST_CASE(a_manual_rescan_is_a_change_after_new_firmware_or_an_unfinished_scan),
        TEST_CASE(folders_deeper_than_the_open_folder_slots_leave_the_walk_incomplete),
        TEST_CASE(finishing_after_a_scan_shows_the_share_of_the_last_walk),
        TEST_CASE(finishing_stops_below_one_hundred_when_the_card_grew),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
