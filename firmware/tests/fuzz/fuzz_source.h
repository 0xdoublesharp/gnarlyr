#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/byte_source.h"

typedef struct FuzzSource {
    const uint8_t *bytes;
    uint32_t size;
    bool invalid_read;
} FuzzSource;

ByteSource fuzz_source(FuzzSource *source);
void fuzz_require(bool condition);
