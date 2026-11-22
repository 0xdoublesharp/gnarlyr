#include "library/favorites.h"

#include "core/bytes.h"
#include "core/record.h"
#include "favorites_format.h"

static bool header_valid(const uint8_t *bytes, uint32_t length)
{
    if (length < KEYS_FIELD + CHECKSUM_SIZE) {
        return false;
    }
    uint32_t count = bytes_le16(bytes + COUNT_FIELD);
    return record_is(bytes, &KIND) && count <= FAVORITES_CAPACITY &&
           length >= KEYS_FIELD + count * KEY_SIZE + CHECKSUM_SIZE;
}

bool favorites_decode(const uint8_t *bytes, uint32_t length, Favorites *favorites)
{
    favorites->count = 0;
    if (!header_valid(bytes, length)) {
        return false;
    }
    uint16_t count = (uint16_t)bytes_le16(bytes + COUNT_FIELD);
    uint32_t checksum_field = KEYS_FIELD + (uint32_t)count * KEY_SIZE;
    if (!record_sealed(bytes, checksum_field)) {
        return false;
    }
    for (uint16_t index = 0; index < count; index++) {
        favorites->keys[index] = bytes_le32(bytes + KEYS_FIELD + (uint32_t)index * KEY_SIZE);
    }
    favorites->count = count;
    return true;
}
