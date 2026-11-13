#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "check.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "music_walk.h"
#include "screens/lyrics_lend.h"

static uint32_t now;
static uint32_t entries;
static uint32_t frames;
static uint32_t last_frame;
static char last_text[160];
static uint32_t lyrics_words[LYRICS_LENT_SECTORS * 128];
static int lyrics_lends;
static uint32_t *lent_window;
static uint32_t lent_sectors;
static int returns;

uint32_t *lyrics_screen_lend_work(void)
{
    lyrics_lends++;
    return lyrics_words;
}

void hal_storage_lend_cache(uint32_t *window, uint32_t sectors)
{
    if (window == NULL) {
        returns++;
        return;
    }
    lent_window = window;
    lent_sectors = sectors;
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
    CHECK(strcmp(title, "Music") == 0);
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
    MusicFingerprint print = music_walk_fingerprint(true);
    CHECK(print.complete);
    CHECK_EQUAL(600, print.entries);
    CHECK_EQUAL(37, frames);
    CHECK(strcmp(last_text, "Checking 1 folders, 592 files\n5 seconds elapsed") == 0);
}

static void a_silent_walk_keeps_the_same_fingerprint_without_drawing(void)
{
    now = entries = frames = last_frame = 0;
    MusicFingerprint print = music_walk_fingerprint(false);
    CHECK(print.complete);
    CHECK_EQUAL(600, print.entries);
    CHECK_EQUAL(0, frames);
}

static void the_walk_lends_the_lyrics_work_to_the_storage_cache_and_takes_it_back(void)
{
    now = entries = frames = last_frame = 0;
    lyrics_lends = returns = 0;
    lent_window = NULL;
    music_walk_fingerprint(false);
    CHECK(lent_window == lyrics_words);
    CHECK_EQUAL(LYRICS_LENT_SECTORS, lent_sectors);
    CHECK_EQUAL(1, returns);
    CHECK_EQUAL(2, lyrics_lends);
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(the_walk_reports_counts_at_most_every_150_ms_and_elapsed_after_five_seconds),
        TEST_CASE(a_silent_walk_keeps_the_same_fingerprint_without_drawing),
        TEST_CASE(the_walk_lends_the_lyrics_work_to_the_storage_cache_and_takes_it_back),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
