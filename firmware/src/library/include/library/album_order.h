#pragma once

#include "core/permutation.h"
#include "library/library_browse.h"

typedef struct AlbumOrder {
    const Library *library;
    LibraryGroup group;
    Permutation permutation;
    uint32_t scanned;
    uint32_t count;
    uint32_t cursor;
    uint32_t album_position;
    LibraryAlbum album;
    bool shuffled;
    bool ready;
    bool failed;
} AlbumOrder;

bool album_order_begin(AlbumOrder *order, const Library *library, BrowseQuery query, uint32_t seed);
bool album_order_step(AlbumOrder *order);
bool album_order_read(void *context, uint16_t first, uint16_t count, uint16_t *tracks);
