#include "device/storage_code.h"

#include "device/image_crc.h"

static uint32_t get32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

bool storage_code_size(const uint8_t *header, uint32_t length, uint32_t *size)
{
    if (length < STORAGE_CODE_HEADER_BYTES || get32(header) != STORAGE_CODE_MAGIC) {
        return false;
    }
    *size = get32(header + 4);
    return *size >= STORAGE_CODE_MINIMUM && *size <= STORAGE_CODE_CAPACITY && *size % 4 == 0;
}

static bool entry_valid(const uint8_t *entry, uint32_t length)
{
    if (get32(entry) != STORAGE_CODE_API_MAGIC || get32(entry + 4) != STORAGE_CODE_ABI ||
        get32(entry + 8) != STORAGE_CODE_API_BYTES) {
        return false;
    }
    for (uint32_t index = 0; index < STORAGE_CODE_FUNCTIONS; index++) {
        uint32_t function = get32(entry + 12 + 4 * index);
        uint32_t address = function & ~UINT32_C(1);
        if (!(function & 1) || address < STORAGE_CODE_BASE + STORAGE_CODE_MINIMUM ||
            address >= STORAGE_CODE_BASE + length) {
            return false;
        }
    }
    return true;
}

bool storage_code_validate(const uint8_t *bytes, uint32_t length, uint32_t main_crc)
{
    uint32_t size = 0;
    return storage_code_size(bytes, length, &size) && size == length &&
           get32(bytes + 12) == main_crc &&
           image_crc_update(0, bytes + STORAGE_CODE_HEADER_BYTES,
                            length - STORAGE_CODE_HEADER_BYTES) == get32(bytes + 8) &&
           entry_valid(bytes + STORAGE_CODE_HEADER_BYTES, length);
}
