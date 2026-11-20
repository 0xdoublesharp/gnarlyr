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

typedef struct FavoritePlaylistAdded {
    uint32_t fingerprint;
    uint16_t count;
    uint16_t tracks[LIBRARY_EXTRA_CAPACITY];
} FavoritePlaylistAdded;

void favorite_playlist_attach(Library *library);
FavoritePlaylistResult favorite_playlist_add(const Library *library, uint16_t track);
bool favorite_playlist_list(const Library *library, uint16_t track, uint16_t *first_added);
const char *favorite_playlist_message(FavoritePlaylistResult result);
