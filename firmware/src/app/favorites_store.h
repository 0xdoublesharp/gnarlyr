#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/favorites.h"
#include "library/library.h"
#include "playback/store.h"

extern const char FAVORITES_STORE_PATH[];

typedef enum FavoriteChange {
    FAVORITE_ADDED,
    FAVORITE_REMOVED,
    FAVORITE_FULL
} FavoriteChange;

typedef struct FavoriteResult {
    FavoriteChange change;
    bool saved;
} FavoriteResult;

typedef struct FavoritesStore {
    Favorites favorites;
    const Library *library;
    uint32_t revision;
    bool unread;
} FavoritesStore;

void favorites_store_load(FavoritesStore *store, const Library *library);
StoreLoad favorites_store_read(Favorites *favorites);
bool favorites_store_contains_album(const FavoritesStore *store, uint16_t album);
FavoriteResult favorites_store_toggle_album(FavoritesStore *store, uint16_t album);
const char *favorites_store_message(FavoriteResult result);
