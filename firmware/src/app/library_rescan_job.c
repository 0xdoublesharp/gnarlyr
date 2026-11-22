#include "library_rescan.h"

#include <stddef.h>

#include "app/app.h"
#include "app/debug_loop.h"
#include "core/bytes.h"
#include "core/drive_path.h"
#include "cover_cache.h"
#include "hal/clock.h"
#include "hal/input.h"
#include "hal/power.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "hidden_scan.h"
#include "library_store.h"
#include "missing_fonts.h"
#include "music_check.h"
#include "scan_guard.h"
#include "ui/theme.h"

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
static ScanGuard guard;
static HalFile guard_file;

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
    RescanProgress progress;
    uint32_t first_album;
    bool combined;
    bool at_root;
} RescanFiles;

static RescanFiles rescan_files;

static bool open_dir(void *context, const char *path, const ScanPlace *place)
{
    RescanFiles *files = context;
    uint32_t since = hal_clock_microseconds();
    HalDirPlace at = {.flags = 0};
    if (place != NULL) {
        at = (HalDirPlace){.start = place->start, .size = place->size, .flags = place->flags};
    }
    bool opened = hal_dir_open_place(&files->dir, path, at);
    files->at_root = files->combined && path[0] == '/' && path[1] == '\0';
    spend(TIME_LIST, since);
    return opened;
}

static bool list_internal_root(RescanFiles *files, ScanEntry *entry)
{
    if (!files->at_root) {
        return false;
    }
    files->at_root = false;
    entry->directory = true;
    entry->modified = 0;
    entry->place = (ScanPlace){.flags = 0};
    entry->name[0] = DRIVE_PATH_INTERNAL;
    entry->name[1] = '\0';
    return true;
}

static bool next_entry(void *context, ScanEntry *entry)
{
    RescanFiles *files = context;
    hal_system_keep_alive();
    uint32_t since = hal_clock_microseconds();
    HalDirEntry listed_entry;
    bool listed = hal_dir_next(&files->dir, &listed_entry);
    spend(TIME_LIST, since);
    if (!listed) {
        return list_internal_root(files, entry);
    }
    entry->directory = listed_entry.directory;
    entry->modified = listed_entry.modified;
    if (entry->directory) {
        HalDirPlace at = hal_dir_place(&files->dir);
        entry->place = (ScanPlace){.start = at.start, .size = at.size, .flags = at.flags};
    }
    size_t index = 0;
    while (index + 1 < sizeof entry->name && listed_entry.name[index] != '\0') {
        entry->name[index] = listed_entry.name[index];
        index++;
    }
    entry->name[index] = '\0';
    return true;
}

static void close_dir(void *context)
{
    RescanFiles *files = context;
    hal_dir_close(&files->dir);
}

static void save_guard(void)
{
    (void)hal_file_write_open(guard_file, SCAN_GUARD_OFFSET, &guard, SCAN_GUARD_BYTES);
}

static void read_guard(void)
{
    guard = (ScanGuard){.magic = 0};
    if (hal_file_open(&guard_file, MUSIC_CHECK_PATH)) {
        (void)hal_file_read(guard_file, SCAN_GUARD_OFFSET, &guard, SCAN_GUARD_BYTES);
    }
}

static void start_guard(uint32_t stops)
{
    read_guard();
    scan_guard_begin(&guard, stops);
    save_guard();
}

static void end_guard(void)
{
    guard.running = 0;
    save_guard();
    hal_file_close(&guard_file);
}

static bool guard_allows(const char *path, uint32_t size)
{
    ScanGuardStep step = scan_guard_open(&guard, scan_guard_mark(path, size, pass == 1));
    if (step == SCAN_GUARD_SAVE) {
        save_guard();
    } else if (step == SCAN_GUARD_SKIP) {
        debug_loop_event(DEBUG_EVENT_STORAGE, guard.seen);
    }
    return step != SCAN_GUARD_SKIP;
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
    if (!guard_allows(path, files->file.size)) {
        hal_file_close(&files->file);
        return false;
    }
    *source = (ByteSource){.read = read_handle, .context = &files->file};
    *size = files->file.size;
    return true;
}

static bool on_card(void *context, const char *card_path)
{
    (void)context;
    HalFile file;
    bool found = hal_file_open(&file, card_path);
    hal_file_close(&file);
    return found;
}

static void close_file(void *context)
{
    RescanFiles *files = context;
    hal_file_close(&files->file);
}

static bool write_handle(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    const HalFile *file = context;
    hal_system_keep_alive();
    uint32_t since = hal_clock_microseconds();
    bool written = hal_file_write_open(*file, offset, bytes, length);
    spend(TIME_WRITE, since);
    return written;
}

static bool write_library(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    hal_system_keep_alive();
    uint32_t since = hal_clock_microseconds();
    bool written = hal_file_write_at(LIBRARY_STORE_PATH, offset, bytes, length);
    spend(TIME_WRITE, since);
    return written;
}

static void report(void *context, uint32_t files, uint32_t tracks)
{
    RescanFiles *rescan = context;
    hal_system_keep_alive();
    rescan->progress.tracks(files, tracks);
}

static void report_phase(void *context, uint32_t phase, uint32_t percent)
{
    RescanFiles *rescan = context;
    hal_system_keep_alive();
    rescan->progress.phase(phase, percent);
}

static bool cable_at_start;

static bool covers_interrupted(void *context)
{
    (void)context;
    return hal_input_waiting() || (!cable_at_start && hal_power_cable());
}

static ScanResult build_into(const ScanJob *scan, RescanFiles *files, CoverStats *stats)
{
    if (!hal_file_open(&files->library, LIBRARY_STORE_PATH)) {
        return SCAN_WRITE_FAILED;
    }
    CoverJob job = {
        .files = scan->files,
        .library = {read_handle, &files->library},
        .library_size = files->library.size,
        .sink = {write_handle, &files->covers},
        .previous = {read_handle, &files->covers},
        .capacity = files->covers.size,
        .memory = scan->memory,
        .size = scan->size,
        .stop = {covers_interrupted, NULL},
        .first_album = files->first_album,
    };
    ScanResult result = cover_build(&job, stats);
    hal_file_close(&files->library);
    return result;
}

static ScanResult build_covers(const ScanJob *scan, CoverStats *stats)
{
    RescanFiles *files = scan->files.context;
    *stats = (CoverStats){.albums = 0, .covers = 0, .reused = 0, .missing = 0};
    if (!hal_file_open(&files->covers, COVER_CACHE_PATH)) {
        return SCAN_NO_ROOM;
    }
    ScanResult result = files->covers.size > 0 ? build_into(scan, files, stats) : SCAN_NO_ROOM;
    hal_file_close(&files->covers);
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
    guard.covers_pending = outcome->result == SCAN_OK ? 1 : guard.covers_pending;
    (void)hal_file_write(RESCAN_TIMES_PATH, spent, sizeof spent);
}

static RescanOutcome rescan_run(uint32_t capacity, HalFile scratch, RescanProgress progress,
                                uint32_t stops)
{
    RescanOutcome outcome = {.result = SCAN_NO_MUSIC_ROOT, .cover_result = SCAN_OK};
    start_guard(stops);
    rescan_files.scratch = scratch;
    rescan_files.progress = progress;
    rescan_files.combined = hal_file_combined();
    uint32_t size = 0;
    void *memory = hal_system_borrow_scratch(&size);
    uint32_t listing_size = 0;
    void *listing = memory != NULL ? hal_system_borrow_listing(&listing_size) : NULL;
    HiddenScan hidden;
    hidden_scan_load(&hidden);
    ScanJob job = {
        .files = {open_dir, next_entry, close_dir, open_file, close_file, &rescan_files},
        .memory = memory,
        .size = size,
        .listing = listing,
        .listing_size = listing_size,
        .scratch = {read_handle, write_handle, &rescan_files.scratch, rescan_files.scratch.size},
        .sink = {write_library, NULL},
        .capacity = capacity,
        .progress = {report, &rescan_files, report_phase},
        .filter = {hidden_scan_skips, &hidden, on_card},
    };
    run_passes(&job, &outcome);
    if (outcome.result == SCAN_OK) {
        hidden_scan_forget_runtime();
        missing_fonts_record(memory, size, app_theme()->fonts.body);
    }
    end_guard();
    hal_file_close(&rescan_files.scratch);
    return outcome;
}

static void note_firmware(void)
{
    uint8_t stored[MUSIC_CHECK_RECORD_BYTES];
    uint8_t current[MUSIC_CHECK_VERSION_BYTES];
    if (!music_check_read_at(0, stored, sizeof stored) ||
        bytes_le32(stored) != MUSIC_CHECK_RECORD_MAGIC) {
        return;
    }
    music_check_put_version(current);
    bool differs = false;
    for (uint32_t index = 0; index < MUSIC_CHECK_VERSION_BYTES; index++) {
        differs |= stored[MUSIC_CHECK_PRINT_BYTES + index] != current[index];
    }
    if (differs) {
        (void)hal_file_write_at(MUSIC_CHECK_PATH, MUSIC_CHECK_PRINT_BYTES, current, sizeof current);
    }
}

static RescanOutcome covers_run(uint32_t stops, uint32_t first_album)
{
    RescanOutcome outcome = {.result = SCAN_OK, .cover_result = SCAN_NO_SCRATCH};
    read_guard();
    bool known = guard.magic == SCAN_GUARD_MAGIC && guard.attempts > 0;
    scan_guard_begin(&guard, known ? guard.attempts - 1u : stops);
    save_guard();
    rescan_files.first_album = first_album;
    cable_at_start = hal_power_cable();
    uint32_t size = 0;
    ScanJob job = {
        .files = {open_dir, next_entry, close_dir, open_file, close_file, &rescan_files},
        .memory = hal_system_borrow_scratch(&size),
    };
    job.size = size;
    pass = 1;
    if (job.memory != NULL) {
        outcome.cover_result =
            scan_guard_covers_allowed(&guard) ? build_covers(&job, &outcome.covers) : SCAN_NO_ROOM;
    }
    guard.covers_pending = outcome.cover_result == SCAN_STOPPED ? 1 : 0;
    end_guard();
    if (outcome.cover_result != SCAN_STOPPED) {
        note_firmware();
    }
    return outcome;
}

const RescanModuleEntry RESCAN_MODULE_ENTRY = {
    .magic = RESCAN_MODULE_MAGIC,
    .run = rescan_run,
    .covers = covers_run,
};
