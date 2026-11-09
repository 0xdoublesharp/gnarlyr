#include "core/hash.h"

uint32_t hash_update(uint32_t hash, const void *bytes, size_t length)
{
    const uint8_t *data = bytes;
    for (size_t index = 0; index < length; index++) {
        hash ^= data[index];
        hash *= HASH_PRIME;
    }
    return hash;
}

void hash_store(uint8_t *bytes, uint32_t hash)
{
    for (int index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(hash >> (8 * index));
    }
}

uint32_t hash_load(const uint8_t *bytes)
{
    uint32_t hash = 0;
    for (int index = 3; index >= 0; index--) {
        hash = hash << 8 | bytes[index];
    }
    return hash;
}
