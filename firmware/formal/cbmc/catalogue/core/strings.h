#pragma once

#include <stdint.h>

typedef enum StringId {
    STR_FORMAL_FIRST,
    STR_FORMAL_SECOND,
    STR_FORMAL_THIRD,
    STR_FORMAL_FOURTH,
    STRING_COUNT
} StringId;

#define STRINGS_HASH UINT32_C(0x5EED5EED)
#define STRING_SEGMENT_COUNT 3
#define STRING_SEGMENT_MODULES {0, 1, 2}
#define STRING_SEGMENT_FIRSTS {0, 2, 3, 4}
