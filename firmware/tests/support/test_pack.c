#include "test_pack.h"

#include <stdio.h>
#include <string.h>

#include "core/hash.h"

static void store_u16(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

void test_pack_seal(uint8_t *pack, uint32_t size)
{
    uint32_t checksum = hash_update(HASH_START, pack, TEST_PACK_CHECKSUM_FIELD);
    checksum = hash_update(checksum, pack + TEST_PACK_CHECKSUM_FIELD + 4,
                           size - TEST_PACK_CHECKSUM_FIELD - 4);
    hash_store(pack + TEST_PACK_CHECKSUM_FIELD, checksum);
}

static void write_header(uint8_t *pack, TestPackLanguage language, uint32_t size)
{
    memcpy(pack, "DSLP", 4);
    store_u16(pack + TEST_PACK_VERSION_FIELD, I18N_VERSION);
    store_u16(pack + TEST_PACK_COUNT_FIELD, STRING_COUNT);
    hash_store(pack + TEST_PACK_SIZE_FIELD, size);
    hash_store(pack + TEST_PACK_CATALOGUE_FIELD, STRINGS_HASH);
    snprintf((char *)pack + TEST_PACK_CODE_FIELD, I18N_CODE_BYTES, "%s", language.code);
    snprintf((char *)pack + TEST_PACK_NAME_FIELD, I18N_NAME_BYTES, "%s", language.name);
}

uint32_t test_pack_build(uint8_t *pack, TestPackLanguage language)
{
    uint32_t used = 0;
    i18n_detach();
    memset(pack, 0, TEST_PACK_CAPACITY);
    for (uint32_t id = 0; id < STRING_COUNT; id++) {
        char text[TEST_PACK_TEXT_CAPACITY];
        const char *english = i18n_text((StringId)id);
        snprintf(text, sizeof text, "%s", id == STR_LANGUAGE_NAME ? language.name : english);
        if (language.translate != NULL && id != STR_LANGUAGE_NAME) {
            language.translate(english, text, sizeof text);
        }
        store_u16(pack + I18N_HEADER_BYTES + 2 * id, used);
        memcpy(pack + TEST_PACK_TEXT_BASE + used, text, strlen(text) + 1);
        used += (uint32_t)strlen(text) + 1;
    }
    uint32_t size = TEST_PACK_TEXT_BASE + used;
    write_header(pack, language, size);
    test_pack_seal(pack, size);
    return size;
}

void test_pack_bracketed(const char *english, char *text, size_t capacity)
{
    snprintf(text, capacity, "[%s]", english);
}

uint32_t test_pack_load(const char *path, uint8_t *pack, uint32_t capacity)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    size_t size = fread(pack, 1, capacity, file);
    fclose(file);
    return (uint32_t)size;
}
