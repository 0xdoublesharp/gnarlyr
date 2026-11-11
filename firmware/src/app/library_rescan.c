#include "library_rescan.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "library_store.h"

static const char MUSIC_ROOT[] = "/";
const char LIBRARY_SCRATCH_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/build.tmp";
const char RESCAN_TIMES_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/rescan_times.bin";

enum {
    MESSAGE_PART_CAPACITY = 48,
    COUNT_CAPACITY = 12
};

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

static bool music_root_exists(void)
{
    HalDir dir;
    if (!hal_dir_open(&dir, MUSIC_ROOT)) {
        return false;
    }
    hal_dir_close(&dir);
    return true;
}

RescanOutcome library_rescan_run(RescanProgress progress)
{
    HalFile scratch;
    RescanOutcome outcome = {.result = SCAN_NO_MUSIC_ROOT, .cover_result = SCAN_OK};
    if (hal_system_restart_pending()) {
        outcome.result = SCAN_RESTART_NEEDED;
        return outcome;
    }
    uint32_t capacity = reserved_size_of(LIBRARY_STORE_PATH);
    if (!music_root_exists()) {
        return outcome;
    }
    if (capacity == 0 || !hal_file_open(&scratch, LIBRARY_SCRATCH_PATH)) {
        outcome.result = SCAN_NO_ROOM;
        return outcome;
    }
    const RescanModuleEntry *entry = hal_system_load_rescan_code();
    if (entry == NULL || entry->magic != RESCAN_MODULE_MAGIC) {
        hal_file_close(&scratch);
        outcome.result = SCAN_NO_CODE;
        return outcome;
    }
    outcome = entry->run(capacity, scratch, progress);
    hal_system_return_scratch();
    return outcome;
}

static const StringId ERROR_TEXT[] = {
    [SCAN_NO_MUSIC_ROOT] = STR_SCAN_NO_MUSIC,
    [SCAN_OUT_OF_MEMORY] = STR_SCAN_NO_MEMORY,
    [SCAN_TOO_MANY] = STR_SCAN_TOO_MANY,
    [SCAN_NO_SCRATCH] = STR_SCAN_NO_SCRATCH,
    [SCAN_FOLDER_TOO_BIG] = STR_SCAN_FOLDER_TOO_BIG,
    [SCAN_PATH_TOO_LONG] = STR_SCAN_PATH_TOO_LONG,
    [SCAN_WRITE_FAILED] = STR_SCAN_WRITE_FAILED,
    [SCAN_NO_ROOM] = STR_SCAN_NO_ROOM,
    [SCAN_NO_CODE] = STR_SCAN_NO_CODE,
    [SCAN_RESTART_NEEDED] = STR_RESTART_TO_FINISH,
};

static const char *error_text(ScanResult result)
{
    bool known = result != SCAN_OK && (size_t)result < sizeof ERROR_TEXT / sizeof ERROR_TEXT[0];
    return known ? i18n_text(ERROR_TEXT[result]) : "";
}

const char *library_rescan_message(RescanOutcome outcome, char *text, uint32_t capacity)
{
    if (outcome.result != SCAN_OK) {
        return error_text(outcome.result);
    }
    char tracks[MESSAGE_PART_CAPACITY];
    char covers[COUNT_CAPACITY];
    StringBuilder builder = string_builder_make(tracks, sizeof tracks);
    i18n_format_counts(&builder,
                       outcome.stats.tracks == 1 ? STR_LIBRARY_ONE_TRACK : STR_LIBRARY_TRACKS,
                       outcome.stats.tracks, 0);
    builder = string_builder_make(covers, sizeof covers);
    string_builder_append_unsigned(&builder, outcome.covers.covers);
    const char *arguments[] = {tracks, covers};
    builder = string_builder_make(text, capacity);
    if (outcome.cover_result != SCAN_OK) {
        i18n_format(&builder, STR_LIBRARY_NO_COVERS, arguments, 2);
    } else if (outcome.covers.albums > 0) {
        i18n_format(&builder,
                    outcome.covers.covers == 1 ? STR_LIBRARY_ONE_COVER : STR_LIBRARY_COVERS,
                    arguments, 2);
    } else {
        string_builder_append(&builder, tracks);
    }
    return text;
}
