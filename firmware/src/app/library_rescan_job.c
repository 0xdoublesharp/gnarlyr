#include "library_rescan.h"

#include <stddef.h>

#include "cover_cache.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "library_store.h"

enum {
    TIME_LIST,
    TIME_OPEN,
    TIME_READ,
    TIME_WRITE,
    TIME_TOTAL,
    TIME_KINDS,
    RESCAN_PASSES = 2,
    TIME_SLOTS = RESCAN_PASSES * TIME_KINDS
};

static uint32_t spent[TIME_SLOTS * 2];
static uint32_t pass;

static void spend(uint32_t kind, uint32_t since)
{
    uint32_t slot = pass * TIME_KINDS + kind;
    spent[slot] += hal_clock_microseconds() - since;
    spent[TIME_SLOTS + slot]++;
}

typedef struct RescanFiles {
    HalDir dir;
    HalFile file;
    HalFile library;
    HalFile covers;
    HalFile scratch;
    HalDirEntry entry;
    RescanProgress progress;
} RescanFiles;

static bool open_dir(void *context, const char *path)
{
    RescanFiles *files = context;
    uint32_t since = hal_clock_microseconds();
    bool opened = hal_dir_open(&files->dir, path);
    spend(TIME_LIST, since);
    return opened;
}

static bool next_entry(void *context, ScanEntry *entry)
{
    RescanFiles *files = context;
    hal_system_keep_alive();
    uint32_t since = hal_clock_microseconds();
    bool listed = hal_dir_next(&files->dir, &files->entry);
    spend(TIME_LIST, since);
    if (!listed) {
        return false;
    }
    entry->directory = files->entry.directory;
    entry->modified = files->entry.modified;
    for (size_t index = 0; index < sizeof entry->name; index++) {
        entry->name[index] = files->entry.name[index];
    }
    return true;
}

static void close_dir(void *context)
{
    RescanFiles *files = context;
    hal_dir_close(&files->dir);
}

static bool read_handle(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const HalFile *file = context;
    hal_system_keep_alive();
    uint32_t since = hal_clock_microseconds();
    bool read = hal_file_read(*file, offset, buffer, length);
    spend(TIME_READ, since);
    return read;
}

static bool open_file(void *context, const char *path, ByteSource *source, uint32_t *size)
{
    RescanFiles *files = context;
    uint32_t since = hal_clock_microseconds();
    bool opened = hal_file_open(&files->file, path);
    spend(TIME_OPEN, since);
    if (!opened) {
        return false;
    }
    *source = (ByteSource){.read = read_handle, .context = &files->file};
    *size = files->file.size;
    return true;
}

static void close_file(void *context)
{
    RescanFiles *files = context;
    hal_file_close(&files->file);
}

static bool timed_write(const char *path, uint32_t offset, const void *bytes, uint32_t length)
{
    uint32_t since = hal_clock_microseconds();
    bool written = hal_file_write_at(path, offset, bytes, length);
    spend(TIME_WRITE, since);
    return written;
}

static bool write_library(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    hal_system_keep_alive();
    return timed_write(LIBRARY_STORE_PATH, offset, bytes, length);
}

static bool write_scratch(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    hal_system_keep_alive();
    return timed_write(LIBRARY_SCRATCH_PATH, offset, bytes, length);
}

static void report(void *context, uint32_t files, uint32_t tracks)
{
    RescanFiles *rescan = context;
    hal_system_keep_alive();
    if (rescan->progress.tracks != NULL) {
        rescan->progress.tracks(files, tracks);
    }
}

static bool write_covers(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    hal_system_keep_alive();
    return timed_write(COVER_CACHE_PATH, offset, bytes, length);
}

static void report_covers(void *context, uint32_t done, uint32_t total)
{
    RescanFiles *rescan = context;
    hal_system_keep_alive();
    if (rescan->progress.covers != NULL) {
        rescan->progress.covers(done, total);
    }
}

static uint32_t reserved_size_of(const char *path)
{
    HalFile file;
    if (!hal_file_open(&file, path)) {
        return 0;
    }
    uint32_t size = file.size;
    hal_file_close(&file);
    return size;
}

static ScanResult build_covers(const ScanJob *scan, CoverStats *stats)
{
    RescanFiles *files = scan->files.context;
    uint32_t capacity = reserved_size_of(COVER_CACHE_PATH);
    *stats = (CoverStats){.albums = 0, .covers = 0, .reused = 0, .missing = 0};
    if (capacity == 0) {
        return SCAN_NO_ROOM;
    }
    if (!hal_file_open(&files->library, LIBRARY_STORE_PATH)) {
        return SCAN_WRITE_FAILED;
    }
    bool previous = hal_file_open(&files->covers, COVER_CACHE_PATH);
    CoverJob job = {
        .files = scan->files,
        .library = {read_handle, &files->library},
        .library_size = files->library.size,
        .sink = {write_covers, NULL},
        .previous = {previous ? read_handle : NULL, &files->covers},
        .capacity = capacity,
        .memory = scan->memory,
        .size = scan->size,
        .progress = {report_covers, files},
    };
    ScanResult result = cover_build(&job, stats);
    if (previous) {
        hal_file_close(&files->covers);
    }
    hal_file_close(&files->library);
    return result;
}

static void run_passes(const ScanJob *job, RescanOutcome *outcome)
{
    for (uint32_t slot = 0; slot < TIME_SLOTS * 2; slot++) {
        spent[slot] = 0;
    }
    pass = 0;
    uint32_t since = hal_clock_microseconds();
    outcome->result = job->memory != NULL ? library_scan(job, &outcome->stats) : SCAN_NO_SCRATCH;
    spend(TIME_TOTAL, since);
    if (outcome->result == SCAN_OK) {
        pass = 1;
        since = hal_clock_microseconds();
        outcome->cover_result = build_covers(job, &outcome->covers);
        spend(TIME_TOTAL, since);
    }
    (void)hal_file_write(RESCAN_TIMES_PATH, spent, sizeof spent);
}

static RescanOutcome rescan_run(uint32_t capacity, HalFile scratch, RescanProgress progress)
{
    static RescanFiles files;
    RescanOutcome outcome = {.result = SCAN_NO_MUSIC_ROOT, .cover_result = SCAN_OK};
    files.scratch = scratch;
    files.progress = progress;
    uint32_t size = 0;
    void *memory = hal_system_borrow_scratch(&size);
    uint32_t listing_size = 0;
    void *listing = memory != NULL ? hal_system_borrow_listing(&listing_size) : NULL;
    ScanJob job = {
        .files = {open_dir, next_entry, close_dir, open_file, close_file, &files},
        .memory = memory,
        .size = size,
        .listing = listing,
        .listing_size = listing_size,
        .scratch = {read_handle, write_scratch, &files.scratch, files.scratch.size},
        .sink = {write_library, NULL},
        .capacity = capacity,
        .progress = {report, &files},
    };
    run_passes(&job, &outcome);
    hal_file_close(&files.scratch);
    return outcome;
}

const RescanModuleEntry RESCAN_MODULE_ENTRY = {
    .magic = RESCAN_MODULE_MAGIC,
    .run = rescan_run,
};
