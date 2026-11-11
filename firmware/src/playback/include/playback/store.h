#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/byte_source.h"

enum {
    STORE_BLOCK_BYTES = 512,
    STORE_FIELD_BYTES = 28,
    STORE_INLINE_BYTES = STORE_BLOCK_BYTES - STORE_FIELD_BYTES,
    STORE_MAGIC_BYTES = 4
};

typedef struct StoreFile {
    ByteSource source;
    bool (*write)(void *context, uint32_t offset, const void *bytes, uint32_t length);
    uint32_t size;
} StoreFile;

typedef enum StoreLoad {
    STORE_EMPTY,
    STORE_READY,
    STORE_UNREADABLE,
    STORE_UNSUPPORTED,
    STORE_CORRUPT
} StoreLoad;

typedef enum StoreResult {
    STORE_IDLE,
    STORE_WORKING,
    STORE_DONE,
    STORE_FAILED,
    STORE_UNCERTAIN
} StoreResult;

typedef struct StoreKind {
    uint8_t magic[STORE_MAGIC_BYTES];
    uint16_t version;
    bool inline_payload;
    uint16_t oldest_version;
} StoreKind;

typedef struct StoreSlot {
    const StoreFile *file;
    const StoreKind *kind;
    uint32_t offset;
} StoreSlot;

typedef struct StoreHeader {
    uint32_t generation;
    uint32_t bytes;
    uint32_t hash;
    uint32_t binding;
    uint16_t count;
    uint16_t version;
} StoreHeader;

typedef union StoreBlock {
    uint8_t bytes[STORE_BLOCK_BYTES];
    uint16_t words[STORE_BLOCK_BYTES / 2];
} StoreBlock;

typedef struct StoreScan {
    uint32_t position;
    uint32_t hash;
} StoreScan;

typedef struct StoreFill {
    bool (*fill)(void *context, uint32_t position, StoreBlock *block, uint32_t count);
    void *context;
} StoreFill;

typedef struct StoreWrite {
    StoreHeader header;
    StoreScan written;
    StoreScan verified;
    uint32_t active;
    bool has_active;
    uint8_t stage;
    StoreResult result;
} StoreWrite;

typedef struct StorePair {
    const StoreFile *file;
    const StoreKind *kind;
    uint32_t bank_bytes;
    StoreLoad (*check)(void *context, uint8_t bank, const StoreHeader *header);
    void *context;
} StorePair;

typedef struct StoreChoice {
    StoreHeader header;
    uint8_t bank;
} StoreChoice;

uint16_t store_get16(const uint8_t *bytes);
uint32_t store_get32(const uint8_t *bytes);
void store_put16(uint8_t *bytes, uint16_t value);
void store_put32(uint8_t *bytes, uint32_t value);
bool store_newer(uint32_t candidate, uint32_t previous);
bool store_blocking(StoreLoad load);

bool store_file_read(const StoreFile *file, uint32_t offset, void *bytes, uint32_t length);
bool store_file_write(const StoreFile *file, uint32_t offset, const void *bytes, uint32_t length);

void store_encode(const StoreKind *kind, const StoreHeader *header, uint8_t *block);
StoreLoad store_decode(const StoreKind *kind, const uint8_t *block, StoreHeader *header);
StoreLoad store_load(const StoreSlot *slot, uint8_t *block, StoreHeader *header);
StoreResult store_publish(const StoreSlot *slot, uint8_t *block, const uint32_t *active);

StoreScan store_scan_begin(void);
uint32_t store_scan_chunk(const StoreScan *scan, uint32_t bytes);
bool store_scan_next(const StoreSlot *slot, StoreScan *scan, uint32_t bytes, uint8_t *block);

void store_write_begin(StoreWrite *job, const uint32_t *active);
StoreResult store_write_step(StoreWrite *job, const StoreSlot *slot, StoreFill fill);
void store_write_abort(StoreWrite *job);
uint8_t store_write_progress(const StoreWrite *job);

StoreLoad store_pair_open(const StorePair *pair, StoreChoice *choice);
