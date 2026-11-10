#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library.h"

enum {
    LIBRARY_CONTENT_BLOCK_BYTES = 512
};

typedef enum LibraryContentState {
    LIBRARY_CONTENT_IDLE,
    LIBRARY_CONTENT_WORKING,
    LIBRARY_CONTENT_READY,
    LIBRARY_CONTENT_FAILED
} LibraryContentState;

typedef struct LibraryContent {
    Library *library;
    uint32_t hash;
    uint32_t position;
    uint8_t part;
    LibraryContentState state;
} LibraryContent;

void library_content_begin(LibraryContent *job, Library *library);
LibraryContentState library_content_step(LibraryContent *job);
bool library_content_fingerprint(const Library *library, uint32_t *fingerprint);
