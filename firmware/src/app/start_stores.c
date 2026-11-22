#include "favorites_store.h"
#include "hal/storage.h"
#include "spoken_store.h"

static bool read_favorites(uint8_t *bytes, uint32_t *length)
{
    HalFile file;
    if (!hal_file_open(&file, FAVORITES_STORE_PATH)) {
        return false;
    }
    *length = file.size < FAVORITES_FILE_CAPACITY ? file.size : FAVORITES_FILE_CAPACITY;
    bool read = hal_file_read(file, 0, bytes, *length);
    hal_file_close(&file);
    return read;
}

StoreLoad favorites_store_read(Favorites *favorites)
{
    uint8_t bytes[FAVORITES_FILE_CAPACITY];
    uint32_t length = 0;
    bool read = read_favorites(bytes, &length) && favorites_decode(bytes, length, favorites);
    return read ? STORE_READY : STORE_EMPTY;
}

static bool read_bookmarks(uint8_t *bytes, uint32_t *length)
{
    HalFile file;
    if (!hal_file_open(&file, SPOKEN_STORE_PATH)) {
        return false;
    }
    *length = file.size < BOOKMARKS_FILE_CAPACITY ? file.size : BOOKMARKS_FILE_CAPACITY;
    bool read = hal_file_read(file, 0, bytes, *length);
    hal_file_close(&file);
    return read;
}

StoreLoad spoken_store_read(Bookmarks *bookmarks)
{
    uint8_t bytes[BOOKMARKS_FILE_CAPACITY];
    uint32_t length = 0;
    bool read = read_bookmarks(bytes, &length) && bookmarks_decode(bytes, length, bookmarks);
    return read ? STORE_READY : STORE_EMPTY;
}
