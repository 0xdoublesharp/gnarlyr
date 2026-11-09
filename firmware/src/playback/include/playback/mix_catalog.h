#pragma once

#include "library/byte_source.h"

enum {
    MIX_CATALOG_SLOTS = 32,
    MIX_NAME_BYTES = 64,
    MIX_PATH_BYTES = 259,
    MIX_RECORD_MAX = 10 + MIX_PATH_BYTES - 1,
    MIX_ENTRY_BYTES = 80,
    MIX_HEADER_BYTES = 512,
    MIX_PAYLOAD_BYTES = MIX_CATALOG_SLOTS * MIX_ENTRY_BYTES,
    MIX_BANK_BYTES = MIX_HEADER_BYTES + MIX_PAYLOAD_BYTES,
    MIX_CATALOG_BYTES = 2 * MIX_BANK_BYTES
};

typedef struct MixIO {
    ByteSource source;
    bool (*write)(void *context, uint32_t offset, const void *bytes, uint32_t length);
    uint32_t size;
} MixIO;

typedef struct MixEntry {
    char name[MIX_NAME_BYTES];
    uint32_t generation;
    uint32_t bytes;
    uint16_t count;
    uint8_t file_bank;
    bool active;
} MixEntry;

typedef enum MixLoad {
    MIX_EMPTY,
    MIX_READY,
    MIX_UNREADABLE,
    MIX_UNSUPPORTED,
    MIX_CORRUPT
} MixLoad;

typedef enum MixResult {
    MIX_IDLE,
    MIX_WORKING,
    MIX_DONE,
    MIX_FAILED,
    MIX_UNCERTAIN
} MixResult;

typedef struct MixCatalog {
    MixIO io;
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
    uint32_t hash;
    uint32_t verify_hash;
    uint16_t position;
    uint8_t bank;
    uint8_t stage;
    MixResult result;
} MixCatalogJob;

MixLoad mix_catalog_open(MixCatalog *catalog, MixIO io);
MixLoad mix_catalog_previous(MixCatalog *catalog);
bool mix_catalog_read(const MixCatalog *catalog, uint16_t slot, MixEntry *entry);
bool mix_catalog_begin(MixCatalogJob *job, MixCatalog *catalog, uint32_t revision, MixPatch patch);
MixResult mix_catalog_step(MixCatalogJob *job);
void mix_catalog_abort(MixCatalogJob *job);
