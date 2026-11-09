#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    BLOCK_READ_CHUNK_SECTORS = 64,
    BLOCK_READ_SECTOR_WORDS = 128
};

typedef struct BlockReader {
    bool (*single)(void *context, uint32_t lba, uint32_t *words);
    bool (*multiple)(void *context, uint32_t lba, uint32_t count, uint32_t *words);
    void *context;
    uint32_t sector_count;
} BlockReader;

bool block_read(const BlockReader *reader, uint32_t lba, uint32_t count, uint32_t *words);
