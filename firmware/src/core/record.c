#include "core/record.h"

#include "core/bytes.h"
#include "core/hash.h"

void record_begin(uint8_t *bytes, const RecordKind *kind)
{
    for (int index = 0; index < RECORD_MAGIC_BYTES; index++) {
        bytes[index] = kind->magic[index];
    }
    bytes_put_le16(bytes + RECORD_MAGIC_BYTES, kind->version);
}

bool record_is(const uint8_t *bytes, const RecordKind *kind)
{
    for (int index = 0; index < RECORD_MAGIC_BYTES; index++) {
        if (bytes[index] != kind->magic[index]) {
            return false;
        }
    }
    return bytes_le16(bytes + RECORD_MAGIC_BYTES) == kind->version;
}

void record_seal(uint8_t *bytes, uint32_t length)
{
    hash_store(bytes + length, hash_update(HASH_START, bytes, length));
}

bool record_sealed(const uint8_t *bytes, uint32_t length)
{
    return hash_load(bytes + length) == hash_update(HASH_START, bytes, length);
}
