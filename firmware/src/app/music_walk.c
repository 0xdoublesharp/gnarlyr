#include "music_walk.h"

#include <stddef.h>

#include "app/app.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "screens/lyrics_lend.h"

typedef struct MusicWalk {
    HalDir dirs[FINGERPRINT_MAX_DEPTH];
    HalDirEntry entry;
    uint32_t started_ms;
    uint32_t shown_ms;
    uint32_t folders;
    uint32_t files;
    uint32_t counted;
    uint32_t expected;
} MusicWalk;

enum {
    NOTICE_PERCENT_TEXT = 64
};

static MusicWalk walk;
static FingerprintWork work;

static void show_counts(const MusicWalk *state, uint32_t now)
{
    char text[160];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_CHECK_COUNTS, state->folders, state->files);
    if (now - state->started_ms >= 5000) {
        string_builder_append_char(&builder, '\n');
        i18n_format_counts(&builder, STR_CHECK_ELAPSED, (now - state->started_ms) / 1000, 0);
    }
    app_show_notice(i18n_text(STR_MUSIC), text);
}

static void show_finishing(const MusicWalk *state)
{
    uint32_t percent = state->counted * 100u / state->expected;
    char text[NOTICE_PERCENT_TEXT];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_SCAN_FINISHING, percent > 99 ? 99 : percent, 0);
    app_show_notice(i18n_text(STR_SCANNING), text);
}

static void show_progress(MusicWalk *state)
{
    if (state->expected == MUSIC_WALK_QUIET) {
        return;
    }
    uint32_t now = hal_clock_milliseconds();
    if (now - state->shown_ms < 150) {
        return;
    }
    state->shown_ms = now;
    if (state->expected == MUSIC_WALK_CHECK) {
        show_counts(state, now);
    } else {
        show_finishing(state);
    }
}

static bool open_dir(void *context, uint32_t level, const char *path)
{
    MusicWalk *state = context;
    hal_system_keep_alive();
    state->folders++;
    show_progress(state);
    return (level > 0 && hal_dir_open(&state->dirs[level], NULL)) ||
           hal_dir_open(&state->dirs[level], path);
}

static bool next_entry(void *context, uint32_t level, FingerprintEntry *entry)
{
    MusicWalk *state = context;
    hal_system_keep_alive();
    if (!hal_dir_next(&state->dirs[level], &state->entry)) {
        return false;
    }
    if (!state->entry.directory) {
        state->files++;
    }
    bool counts = state->entry.name[0] != '.' &&
                  music_fingerprint_counts(state->entry.name, state->entry.directory);
    state->counted += counts ? 1u : 0u;
    if ((state->files & 15u) == 0) {
        show_progress(state);
    }
    entry->directory = state->entry.directory;
    entry->size = state->entry.size;
    entry->modified = state->entry.modified;
    for (size_t index = 0; index < sizeof entry->name; index++) {
        entry->name[index] = state->entry.name[index];
    }
    return true;
}

static void close_dir(void *context, uint32_t level)
{
    MusicWalk *state = context;
    hal_dir_close(&state->dirs[level]);
}

MusicFingerprint music_walk_fingerprint(uint32_t expected)
{
    walk.expected = expected;
    walk.folders = 0;
    walk.files = 0;
    walk.counted = 0;
    walk.started_ms = hal_clock_milliseconds();
    walk.shown_ms = walk.started_ms;
    FingerprintFiles files = {open_dir, next_entry, close_dir, &walk};
    hal_storage_lend_cache(lyrics_screen_lend_work(), LYRICS_LENT_SECTORS);
    MusicFingerprint print = music_fingerprint(&files, &work);
    hal_storage_lend_cache(NULL, 0);
    lyrics_screen_lend_work();
    return print;
}
