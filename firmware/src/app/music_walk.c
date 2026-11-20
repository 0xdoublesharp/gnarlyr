#include "music_walk.h"

#include <stddef.h>

#include "app/app.h"
#include "core/deadline.h"
#include "core/drive_path.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "music_check.h"
#include "music_record.h"

typedef struct MusicWalk {
    HalDir dirs[FINGERPRINT_MAX_DEPTH];
    HalDirEntry entry;
    uint32_t started_ms;
    uint32_t shown_ms;
    uint32_t folders;
    uint32_t files;
    uint32_t counted;
    uint32_t songs;
    uint32_t expected;
    bool internal;
    bool overflowed;
} MusicWalk;

enum {
    NOTICE_PERCENT_TEXT = 64,
    LENT_SECTORS = 5,
    SECTOR_WORDS = 128
};

static MusicWalk walk;
static FingerprintWork work;
static uint32_t lent[LENT_SECTORS * SECTOR_WORDS];

static void show_counts(const MusicWalk *state, uint32_t now)
{
    char text[160];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_CHECK_COUNTS, state->folders, state->files);
    if (deadline_elapsed(now, state->started_ms, 5000)) {
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
    if (!deadline_elapsed(now, state->shown_ms, 150)) {
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
    if (level >= HAL_OPEN_DIRS) {
        state->overflowed = true;
        return false;
    }
    if (level > 0 && hal_dir_open(&state->dirs[level], NULL)) {
        return true;
    }
    if (state->internal) {
        return level == 0 && hal_dir_open(&state->dirs[level], DRIVE_PATH_INTERNAL_ROOT);
    }
    return hal_dir_open(&state->dirs[level], path);
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
    state->songs += counts && !state->entry.directory ? 1u : 0u;
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

static MusicFingerprint walk_drive(bool internal)
{
    walk.internal = internal;
    walk.songs = 0;
    walk.overflowed = false;
    FingerprintFiles files = {open_dir, next_entry, close_dir, &walk};
    hal_storage_lend_cache(internal ? NULL : lent, internal ? 0 : LENT_SECTORS);
    MusicFingerprint print = music_fingerprint(&files, &work);
    hal_storage_lend_cache(NULL, 0);
    print.complete = print.complete && !walk.overflowed;
    return print;
}

static void begin(uint32_t expected)
{
    walk.expected = expected;
    walk.folders = 0;
    walk.files = 0;
    walk.counted = 0;
    walk.started_ms = hal_clock_milliseconds();
    walk.shown_ms = walk.started_ms;
}

MusicFingerprint music_walk_fingerprint(uint32_t expected)
{
    begin(expected);
    return walk_drive(false);
}

static MusicRecord walk_drives(uint32_t expected)
{
    begin(expected);
    MusicRecord current = {.card = walk_drive(false),
                           .internal = {.complete = true},
                           .internal_state = MUSIC_DRIVE_NONE,
                           .generation = music_check_generation()};
    if (hal_file_combined()) {
        current.internal = walk_drive(true);
        current.internal_state = MUSIC_DRIVE_KNOWN;
    }
    return current;
}

static uint32_t expected_entries(bool known, const MusicRecord *stored, uint32_t files)
{
    uint32_t entries = known ? stored->card.entries + stored->internal.entries : 0;
    return entries > 0 ? entries : files + files / 4u + 1u;
}

static void remember(const MusicRecord *current)
{
    if (current->card.complete && current->internal.complete) {
        music_record_write(current);
    }
}

bool music_walk_run(uint32_t files)
{
    if (files == MUSIC_WALK_MANUAL) {
        return music_check_firmware_is_new() || !music_record_settled();
    }
    MusicRecord stored = {.internal_state = MUSIC_DRIVE_NONE};
    bool known = music_record_read(&stored);
    bool check = files == MUSIC_WALK_CHECK;
    MusicRecord current =
        walk_drives(check ? MUSIC_WALK_CHECK : expected_entries(known, &stored, files));
    bool same = check && known && music_record_same_music(&stored, &current, walk.songs);
    if (!check || (same && (stored.generation != current.generation ||
                            stored.internal_state != current.internal_state))) {
        remember(&current);
    }
    return check && !same;
}
