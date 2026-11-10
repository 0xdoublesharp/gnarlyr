#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/store.h"

enum {
    MIX_CATALOG_SLOTS = 32,
    MIX_NAME_BYTES = 64,
    MIX_ENTRY_BYTES = 80,
    MIX_PAYLOAD_BYTES = MIX_CATALOG_SLOTS * MIX_ENTRY_BYTES,
    MIX_BANK_BYTES = STORE_BLOCK_BYTES + MIX_PAYLOAD_BYTES,
    MIX_CATALOG_BYTES = 2 * MIX_BANK_BYTES
};

extern const char MIX_CATALOG_PATH[];

typedef struct MixEntry {
    char name[MIX_NAME_BYTES];
    uint32_t generation;
    uint32_t bytes;
    uint16_t count;
    uint8_t file_bank;
    bool active;
} MixEntry;

typedef struct MixCatalog {
    StoreFile file;
    uint32_t generation;
    uint8_t bank;
    bool valid;
    bool busy;
    bool needs_reload;
} MixCatalog;

typedef struct MixPatch {
    MixEntry entry;
    uint16_t slot;
} MixPatch;

typedef struct MixCatalogJob {
    MixCatalog *catalog;
    MixPatch patch;
    StoreWrite write;
    uint8_t bank;
} MixCatalogJob;

StoreLoad mix_catalog_open(MixCatalog *catalog, StoreFile file);
StoreLoad mix_catalog_previous(MixCatalog *catalog);
bool mix_catalog_read(const MixCatalog *catalog, uint16_t slot, MixEntry *entry);
bool mix_catalog_begin(MixCatalogJob *job, MixCatalog *catalog, uint32_t revision, MixPatch patch);
StoreResult mix_catalog_step(MixCatalogJob *job);
void mix_catalog_abort(MixCatalogJob *job);
