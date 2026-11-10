#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/i18n.h"

enum {
    TEST_PACK_CAPACITY = 4 * I18N_PACK_CAPACITY,
    TEST_PACK_TEXT_CAPACITY = 256,
    TEST_PACK_VERSION_FIELD = 4,
    TEST_PACK_COUNT_FIELD = 6,
    TEST_PACK_SIZE_FIELD = 8,
    TEST_PACK_CATALOGUE_FIELD = 12,
    TEST_PACK_CHECKSUM_FIELD = 16,
    TEST_PACK_CODE_FIELD = 20,
    TEST_PACK_NAME_FIELD = 28,
    TEST_PACK_TEXT_BASE = I18N_HEADER_BYTES + 2 * STRING_COUNT
};

typedef void (*TestPackText)(const char *english, char *text, size_t capacity);

typedef struct TestPackLanguage {
    const char *code;
    const char *name;
    TestPackText translate;
} TestPackLanguage;

uint32_t test_pack_build(uint8_t *pack, TestPackLanguage language);
void test_pack_seal(uint8_t *pack, uint32_t size);
void test_pack_bracketed(const char *english, char *text, size_t capacity);
uint32_t test_pack_load(const char *path, uint8_t *pack, uint32_t capacity);
