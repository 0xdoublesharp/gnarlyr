#include "core/hash.h"

#include "formal.h"

uint32_t hash_update(uint32_t hash, const void *bytes, size_t length)
{
    (void)hash;
    __CPROVER_assert(__CPROVER_r_ok(bytes, length), "a hashed span is readable");
    return nondet_uint32();
}

void hash_store(uint8_t *bytes, uint32_t hash)
{
    for (int index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(hash >> (8 * index));
    }
}

uint32_t hash_load(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}
