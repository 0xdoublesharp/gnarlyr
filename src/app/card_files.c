#include "card_files.h"

#include <stddef.h>
#include <stdint.h>

#include "bluetooth_store.h"
#include "cover_cache.h"
#include "favorites_store.h"
#include "hal/storage.h"
#include "library_rescan.h"
#include "library_store.h"
#include "music_check.h"
#include "player.h"
#include "settings_store.h"
#include "spoken_store.h"

enum {
    SMALL_FILE_BYTES = 4096,
    LIBRARY_FILE_BYTES = 4 * 1024 * 1024,
    COVERS_FILE_BYTES = 80 * 1024 * 1024,
    SCRATCH_FILE_BYTES = 24 * 1024 * 1024
};

typedef struct CardFile {
    const char *path;
    uint32_t size;
} CardFile;

static const CardFile CARD_FILES[] = {
    {LIBRARY_STORE_PATH, LIBRARY_FILE_BYTES},   {COVER_CACHE_PATH, COVERS_FILE_BYTES},
    {LIBRARY_SCRATCH_PATH, SCRATCH_FILE_BYTES}, {SETTINGS_STORE_PATH, SMALL_FILE_BYTES},
    {PLAYER_RESUME_PATH, SMALL_FILE_BYTES},     {FAVORITES_STORE_PATH, SMALL_FILE_BYTES},
    {MUSIC_CHECK_PATH, SMALL_FILE_BYTES},       {PLAYER_UP_NEXT_PATH, SMALL_FILE_BYTES},
    {SPOKEN_STORE_PATH, SMALL_FILE_BYTES},      {RESCAN_TIMES_PATH, SMALL_FILE_BYTES},
    {BLUETOOTH_STORE_PATH, SMALL_FILE_BYTES},
};

size_t card_files_count(void)
{
    return sizeof CARD_FILES / sizeof CARD_FILES[0];
}

bool card_files_slow(size_t index)
{
    return CARD_FILES[index].size >= LIBRARY_FILE_BYTES;
}

const char *card_files_path(size_t index)
{
    return CARD_FILES[index].path;
}

bool card_files_missing(size_t index)
{
    HalFile file;
    if (index >= card_files_count()) {
        return false;
    }
    if (!hal_file_open(&file, CARD_FILES[index].path)) {
        return true;
    }
    hal_file_close(&file);
    return false;
}

void card_files_reserve(CardFilesReport *report, size_t index)
{
    if (index >= card_files_count()) {
        return;
    }
    HalReserveResult result = hal_file_reserve(CARD_FILES[index].path, CARD_FILES[index].size);
    report->card_full |= result == HAL_RESERVE_NO_SPACE;
    report->library_created |=
        result == HAL_RESERVE_CREATED && CARD_FILES[index].path == LIBRARY_STORE_PATH;
}
