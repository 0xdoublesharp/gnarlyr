#pragma once

#include "library/library_browse.h"
#include "library/spoken_path.h"

typedef struct SpokenShelf {
    SpokenKind kind;
    uint32_t root;
    uint32_t child;
    uint32_t track;
    uint32_t position;
    uint32_t last_position;
    BrowseItem last;
    bool cached;
    void (*between)(void *context);
    void *context;
} SpokenShelf;

void spoken_shelf_init(SpokenShelf *shelf, SpokenKind kind, void (*between)(void *context),
                       void *context);
bool spoken_shelf_present(const Library *library, SpokenKind kind);
uint32_t spoken_shelf_count(const Library *library, SpokenShelf *shelf);
bool spoken_shelf_item(const Library *library, SpokenShelf *shelf, uint32_t position,
                       BrowseItem *item);
