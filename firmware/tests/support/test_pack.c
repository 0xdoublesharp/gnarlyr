#include "test_pack.h"

#include <stdio.h>
#include <string.h>

#include "core/hash.h"

enum {
    ROW_COUNT = 2,
    ROW_OFFSET = 4,
    ROW_SIZE = 8,
    ROW_CHECKSUM = 12,
    MEMORY_ALIGN = 4
};

static const uint16_t FIRSTS[STRING_SEGMENT_COUNT + 1] = STRING_SEGMENT_FIRSTS;
static const uint8_t MODULES[STRING_SEGMENT_COUNT] = STRING_SEGMENT_MODULES;

static void store_u16(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static uint8_t *row_of(uint8_t *pack, uint32_t segment)
{
    return pack + TEST_PACK_ROWS_FIELD + segment * I18N_SEGMENT_BYTES;
}

void test_pack_seal(uint8_t *pack, uint32_t size)
{
    (void)size;
    for (uint32_t segment = 0; segment < STRING_SEGMENT_COUNT; segment++) {
        uint8_t *row = row_of(pack, segment);
        uint32_t offset = hash_load(row + ROW_OFFSET);
        uint32_t length = hash_load(row + ROW_SIZE);
        if (length <= TEST_PACK_CAPACITY && offset <= TEST_PACK_CAPACITY - length) {
            hash_store(row + ROW_CHECKSUM, hash_update(HASH_START, pack + offset, length));
        }
    }
    uint32_t checksum = hash_update(HASH_START, pack, TEST_PACK_CHECKSUM_FIELD);
    checksum = hash_update(checksum, pack + TEST_PACK_CHECKSUM_FIELD + 4,
                           I18N_INDEX_BYTES - TEST_PACK_CHECKSUM_FIELD - 4);
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
    store_u16(pack + TEST_PACK_SEGMENTS_FIELD, STRING_SEGMENT_COUNT);
}

static uint32_t write_segment(uint8_t *block, uint32_t segment, TestPackLanguage language)
{
    uint32_t strings = (uint32_t)FIRSTS[segment + 1] - FIRSTS[segment];
    uint32_t used = 0;
    for (uint32_t index = 0; index < strings; index++) {
        uint32_t id = FIRSTS[segment] + index;
        char text[TEST_PACK_TEXT_CAPACITY];
        const char *english = i18n_text((StringId)id);
        snprintf(text, sizeof text, "%s", id == STR_LANGUAGE_NAME ? language.name : english);
        if (language.translate != NULL && id != STR_LANGUAGE_NAME) {
            language.translate(english, text, sizeof text);
        }
        store_u16(block + 2 * index, used);
        memcpy(block + 2 * strings + used, text, strlen(text) + 1);
        used += (uint32_t)strlen(text) + 1;
    }
    return 2 * strings + used;
}

uint32_t test_pack_build(uint8_t *pack, TestPackLanguage language)
{
    uint32_t offset = I18N_INDEX_BYTES;
    i18n_detach();
    memset(pack, 0, TEST_PACK_CAPACITY);
    for (uint32_t segment = 0; segment < STRING_SEGMENT_COUNT; segment++) {
        uint32_t size = write_segment(pack + offset, segment, language);
        uint8_t *row = row_of(pack, segment);
        store_u16(row, MODULES[segment]);
        store_u16(row + ROW_COUNT, (uint32_t)FIRSTS[segment + 1] - FIRSTS[segment]);
        hash_store(row + ROW_OFFSET, offset);
        hash_store(row + ROW_SIZE, size);
        offset += size;
    }
    write_header(pack, language, offset);
    test_pack_seal(pack, offset);
    return offset;
}

void test_pack_bracketed(const char *english, char *text, size_t capacity)
{
    snprintf(text, capacity, "[%s]", english);
}

void test_pack_upper_case(const char *english, char *text, size_t capacity)
{
    snprintf(text, capacity, "%s", english);
    for (char *cursor = text; *cursor != '\0'; cursor++) {
        *cursor = *cursor >= 'a' && *cursor <= 'z' ? (char)(*cursor - 'a' + 'A') : *cursor;
    }
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

uint32_t test_pack_segment_of(StringId id)
{
    uint32_t segment = 0;
    while ((uint32_t)id >= FIRSTS[segment + 1]) {
        segment++;
    }
    return segment;
}

I18nSegment test_pack_segment(const uint8_t *pack, uint32_t segment)
{
    const uint8_t *row = pack + TEST_PACK_ROWS_FIELD + segment * I18N_SEGMENT_BYTES;
    return (I18nSegment){hash_load(row + ROW_OFFSET), hash_load(row + ROW_SIZE),
                         hash_load(row + ROW_CHECKSUM)};
}

uint32_t test_pack_resident_end(const uint8_t *pack)
{
    I18nSegment resident = test_pack_segment(pack, 0);
    return resident.offset + resident.size;
}

uint32_t test_pack_memory_need(const uint8_t *pack)
{
    uint32_t largest = 0;
    for (uint32_t segment = 1; segment < STRING_SEGMENT_COUNT; segment++) {
        uint32_t size = test_pack_segment(pack, segment).size;
        largest = size > largest ? size : largest;
    }
    uint32_t resident = test_pack_resident_end(pack);
    return (resident + MEMORY_ALIGN - 1) / MEMORY_ALIGN * MEMORY_ALIGN + largest;
}

bool test_pack_attach_segment(const uint8_t *pack, uint32_t segment)
{
    I18nSegment row;
    return i18n_pack_segment(pack, segment, &row) &&
           i18n_attach_segment(segment, pack + row.offset, &row);
}

const char *test_pack_text(const uint8_t *pack, StringId id)
{
    uint32_t segment = test_pack_segment_of(id);
    if (segment != 0 && i18n_segment_loaded() != segment) {
        test_pack_attach_segment(pack, segment);
    }
    return i18n_text(id);
}
