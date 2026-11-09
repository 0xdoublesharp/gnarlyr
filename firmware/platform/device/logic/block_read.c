#include "device/block_read.h"

static bool read_each(const BlockReader *reader, uint32_t lba, uint32_t count, uint32_t *words)
{
    for (uint32_t index = 0; index < count; index++) {
        if (!reader->single(reader->context, lba + index,
                            words + index * BLOCK_READ_SECTOR_WORDS)) {
            return false;
        }
    }
    return true;
}

static bool read_chunk(const BlockReader *reader, uint32_t lba, uint32_t count, uint32_t *words)
{
    if (count > 1 && reader->multiple(reader->context, lba, count, words)) {
        return true;
    }
    return read_each(reader, lba, count, words);
}

bool block_read(const BlockReader *reader, uint32_t lba, uint32_t count, uint32_t *words)
{
    if (count == 0) {
        return true;
    }
    if (lba >= reader->sector_count || count > reader->sector_count - lba) {
        return false;
    }
    while (count > 0) {
        uint32_t chunk = count < BLOCK_READ_CHUNK_SECTORS ? count : BLOCK_READ_CHUNK_SECTORS;
        if (!read_chunk(reader, lba, chunk, words)) {
            return false;
        }
        lba += chunk;
        count -= chunk;
        words += chunk * BLOCK_READ_SECTOR_WORDS;
    }
    return true;
}
