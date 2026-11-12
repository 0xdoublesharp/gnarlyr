#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/storage.h"
#include "library/block_cache.h"
#include "library/library.h"

enum {
    HIDDEN_TRACKS_MAGIC = 0x44485344,
    HIDDEN_TRACKS_HEAD_BYTES = 512,
    HIDDEN_TRACKS_RECORD_BYTES = 272,
    HIDDEN_TRACKS_RECORDS = 16,
    HIDDEN_TRACKS_FILE_BYTES = HIDDEN_TRACKS_HEAD_BYTES +
        HIDDEN_TRACKS_RECORDS * HIDDEN_TRACKS_RECORD_BYTES
};

typedef struct HiddenTracksHead {
    uint32_t magic;
    uint32_t fingerprint;
    uint16_t records;
    uint16_t reserved;
    LibraryHidden set;
} HiddenTracksHead;

_Static_assert(sizeof(HiddenTracksHead) <= HIDDEN_TRACKS_HEAD_BYTES, "head fits its block");

typedef struct LibraryStore {
    HalFile file;
    BlockCache cache;
    Library library;
    LibraryHidden hidden;
    bool available;
} LibraryStore;

extern const char LIBRARY_STORE_PATH[];
extern const char HIDDEN_TRACKS_PATH[];

bool library_store_open(LibraryStore *store);
