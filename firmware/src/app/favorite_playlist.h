#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library.h"

extern const char FAVORITE_PLAYLIST_PATH[];
extern const char FAVORITE_ADDED_PATH[];

typedef enum FavoritePlaylistResult {
    FAVORITE_PLAYLIST_ADDED,
    FAVORITE_PLAYLIST_PRESENT,
    FAVORITE_PLAYLIST_FAILED
} FavoritePlaylistResult;

void favorite_playlist_attach(Library *library);
FavoritePlaylistResult favorite_playlist_add(const Library *library, uint16_t track);
void favorite_playlist_remember(const Library *library, uint16_t track);
const char *favorite_playlist_message(FavoritePlaylistResult result);
