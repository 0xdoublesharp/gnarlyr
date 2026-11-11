#pragma once

#include <stdint.h>

#include "core/hash.h"

enum {
    LIBRARY_GAIN_MAGIC = 0x47525344,
    LIBRARY_GAIN_HEADER_SIZE = 20,
    LIBRARY_GAIN_RECORD_SIZE = 20,
    LIBRARY_GAIN_VERSION = 1
};

static inline uint32_t library_gain_record_hash(uint32_t fingerprint, uint32_t index,
                                                const uint8_t *record)
{
    uint8_t prefix[8];
    hash_store(prefix, fingerprint);
    hash_store(prefix + 4, index);
    return hash_update(hash_update(HASH_START, prefix, sizeof prefix), record, 16);
}
