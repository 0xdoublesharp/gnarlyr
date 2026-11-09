#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    STORAGE_CODE_BASE = 0x0308F000,
    STORAGE_CODE_CAPACITY = 0x3000,
    STORAGE_CODE_IMAGE_OFFSET = 0x218000,
    STORAGE_CODE_HEADER_BYTES = 16,
    STORAGE_CODE_MAGIC = 0x4F545345,
    STORAGE_CODE_API_MAGIC = 0x49504153,
    STORAGE_CODE_ABI = 1,
    STORAGE_CODE_FUNCTIONS = 11,
    STORAGE_CODE_API_BYTES = 12 + 4 * STORAGE_CODE_FUNCTIONS,
    STORAGE_CODE_MINIMUM = STORAGE_CODE_HEADER_BYTES + STORAGE_CODE_API_BYTES,
    RESCAN_CODE_CAPACITY = STORAGE_CODE_BASE - 0x0307A000
};

bool storage_code_size(const uint8_t *header, uint32_t length, uint32_t *size);
bool storage_code_validate(const uint8_t *bytes, uint32_t length, uint32_t main_crc);
