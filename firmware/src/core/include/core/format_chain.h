#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum FormatAge {
    FORMAT_AGE_INVALID,
    FORMAT_AGE_OLDER,
    FORMAT_AGE_CURRENT,
    FORMAT_AGE_NEWER
} FormatAge;

typedef struct FormatBytes {
    uint8_t *bytes;
    uint32_t length;
    uint32_t capacity;
} FormatBytes;

typedef bool (*FormatUpgrade)(FormatBytes *file);

typedef struct FormatChain {
    uint16_t current;
    const FormatUpgrade *steps;
} FormatChain;

FormatAge format_age(uint16_t current, uint32_t version);
bool format_upgrade(const FormatChain *chain, uint32_t version, FormatBytes *file);
