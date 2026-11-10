#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    HAL_LANGUAGE_COPIES = 2
};

typedef struct HalLanguagePack {
    uint8_t copy;
    uint8_t index;
} HalLanguagePack;

uint8_t *hal_language_memory(uint32_t *capacity);
bool hal_language_read(HalLanguagePack pack, uint32_t offset, void *buffer, uint32_t length);
