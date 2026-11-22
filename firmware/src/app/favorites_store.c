#include "favorites_store.h"

#include "core/i18n.h"
#include "hal/storage.h"

const char FAVORITES_STORE_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/favorites.bin";

void favorites_store_load(FavoritesStore *store, const Library *library)
{
    store->library = library;
    store->revision++;
    StoreLoad load = favorites_store_read(&store->favorites);
    store->unread = load == STORE_UNSUPPORTED;
    if (load != STORE_READY) {
        favorites_clear(&store->favorites);
    }
}

bool favorites_store_contains_album(const FavoritesStore *store, uint16_t album)
{
    return favorites_contains(&store->favorites, library_album_key(store->library, album));
}

static bool save(const FavoritesStore *store)
{
    if (store->unread) {
        return false;
    }
    uint8_t bytes[FAVORITES_FILE_CAPACITY];
    uint32_t length = favorites_encode(&store->favorites, bytes);
    return hal_file_write(FAVORITES_STORE_PATH, bytes, length);
}

FavoriteResult favorites_store_toggle_album(FavoritesStore *store, uint16_t album)
{
    uint32_t key = library_album_key(store->library, album);
    bool present = favorites_contains(&store->favorites, key);
    if (!present && store->favorites.count == FAVORITES_CAPACITY) {
        return (FavoriteResult){.change = FAVORITE_FULL, .saved = true};
    }
    favorites_toggle(&store->favorites, key);
    store->revision++;
    FavoriteChange change = present ? FAVORITE_REMOVED : FAVORITE_ADDED;
    return (FavoriteResult){.change = change, .saved = save(store)};
}

const char *favorites_store_message(FavoriteResult result)
{
    if (!result.saved) {
        return i18n_text(STR_FAVORITES_NOT_SAVED);
    }
    switch (result.change) {
    case FAVORITE_ADDED:
        return i18n_text(STR_FAVORITE_ADDED);
    case FAVORITE_REMOVED:
        return i18n_text(STR_FAVORITE_REMOVED);
    case FAVORITE_FULL:
        return i18n_text(STR_FAVORITES_FULL);
    }
    return "";
}
