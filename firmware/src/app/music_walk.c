#include "music_walk.h"

#include <stddef.h>

#include "hal/storage.h"
#include "hal/system.h"

typedef struct MusicWalk {
    HalDir dirs[FINGERPRINT_MAX_DEPTH];
    HalDirEntry entry;
} MusicWalk;

static MusicWalk walk;
static FingerprintWork work;

static bool open_dir(void *context, uint32_t level, const char *path)
{
    MusicWalk *state = context;
    hal_system_keep_alive();
    return hal_dir_open(&state->dirs[level], path);
}

static bool next_entry(void *context, uint32_t level, FingerprintEntry *entry)
{
    MusicWalk *state = context;
    hal_system_keep_alive();
    if (!hal_dir_next(&state->dirs[level], &state->entry)) {
        return false;
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

MusicFingerprint music_walk_fingerprint(void)
{
    FingerprintFiles files = {open_dir, next_entry, close_dir, &walk};
    return music_fingerprint(&files, &work);
}
