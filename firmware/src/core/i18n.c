#include "core/i18n.h"

#include "core/bytes.h"
#include "core/hash.h"

#ifdef ECHO_BUILTIN_ENGLISH
#include "strings_english.h"
#endif

enum {
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    COUNT_FIELD = 6,
    SIZE_FIELD = 8,
    CATALOGUE_FIELD = 12,
    CHECKSUM_FIELD = 16,
    CHECKSUM_END = 20,
    CODE_FIELD = 20,
    NAME_FIELD = 28,
    SEGMENTS_FIELD = 56,
    ROWS_FIELD = 60,
    ROW_COUNT = 2,
    ROW_OFFSET = 4,
    ROW_SIZE = 8,
    ROW_CHECKSUM = 12,
    OFFSET_BYTES = 2,
    NUMBER_CAPACITY = 12
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'D', 'S', 'L', 'P'};
static const uint16_t FIRSTS[STRING_SEGMENT_COUNT + 1] = STRING_SEGMENT_FIRSTS;

static const uint8_t *attached;
static const uint8_t *module_block;
static uint32_t module_segment;

static uint32_t strings_in(uint32_t segment)
{
    return (uint32_t)FIRSTS[segment + 1] - FIRSTS[segment];
}

_Static_assert(sizeof(I18nSegment) == I18N_SEGMENT_BYTES - ROW_OFFSET, "a row ends in its span");

static bool row_matches(const uint8_t *index, uint32_t segment, I18nSegment *row)
{
    const uint8_t *at = index + ROWS_FIELD + segment * I18N_SEGMENT_BYTES;
    __builtin_memcpy(row, at + ROW_OFFSET, sizeof *row);
    return bytes_le16(at + ROW_COUNT) == strings_in(segment) &&
           row->size - 1 - OFFSET_BYTES * strings_in(segment) < I18N_FILE_CAPACITY;
}

static bool header_matches(const uint8_t *header)
{
    for (int index = 0; index < MAGIC_SIZE; index++) {
        if (header[index] != MAGIC[index]) {
            return false;
        }
    }
    uint32_t checksum = hash_update(HASH_START, header, CHECKSUM_FIELD);
    checksum = hash_update(checksum, header + CHECKSUM_END, I18N_INDEX_BYTES - CHECKSUM_END);
    return bytes_le16(header + VERSION_FIELD) == I18N_VERSION &&
           bytes_le16(header + COUNT_FIELD) == STRING_COUNT &&
           hash_load(header + CATALOGUE_FIELD) == STRINGS_HASH &&
           bytes_le16(header + SEGMENTS_FIELD) == STRING_SEGMENT_COUNT &&
           checksum == hash_load(header + CHECKSUM_FIELD) &&
           header[CODE_FIELD + I18N_CODE_BYTES - 1] == '\0' &&
           header[NAME_FIELD + I18N_NAME_BYTES - 1] == '\0';
}

bool i18n_pack_info(const uint8_t *index, size_t length, I18nPackInfo *info)
{
    I18nSegment resident;
    if (index == NULL || length < I18N_INDEX_BYTES || !header_matches(index) ||
        !row_matches(index, 0, &resident) || resident.offset != I18N_INDEX_BYTES) {
        return false;
    }
    info->size = hash_load(index + SIZE_FIELD);
    info->resident = resident.offset + resident.size;
    for (int at = 0; at < I18N_CODE_BYTES; at++) {
        info->code[at] = (char)index[CODE_FIELD + at];
    }
    for (int at = 0; at < I18N_NAME_BYTES; at++) {
        info->name[at] = (char)index[NAME_FIELD + at];
    }
    return true;
}

bool i18n_pack_segment(const uint8_t *index, uint32_t segment, I18nSegment *row)
{
    return row_matches(index, segment, row);
}

static bool block_is_sound(const uint8_t *block, uint32_t segment, const I18nSegment *row)
{
    uint32_t table = OFFSET_BYTES * strings_in(segment);
    if (hash_update(HASH_START, block, row->size) != row->checksum ||
        block[row->size - 1] != '\0') {
        return false;
    }
    for (uint32_t index = 0; index < strings_in(segment); index++) {
        if (table + bytes_le16(block + index * OFFSET_BYTES) >= row->size) {
            return false;
        }
    }
    return true;
}

bool i18n_attach(const uint8_t *pack, size_t length)
{
    I18nPackInfo info;
    I18nSegment row;
    i18n_detach();
    if (!i18n_pack_info(pack, length, &info) || info.resident > length ||
        !row_matches(pack, 0, &row) || !block_is_sound(pack + I18N_INDEX_BYTES, 0, &row)) {
        return false;
    }
    attached = pack;
    return true;
}

void i18n_detach_segment(void)
{
    module_block = NULL;
    module_segment = 0;
}

bool i18n_attach_segment(uint32_t segment, const uint8_t *block, const I18nSegment *row)
{
    i18n_detach_segment();
    if (segment - 1 >= STRING_SEGMENT_COUNT - 1 || !block_is_sound(block, segment, row)) {
        return false;
    }
    module_block = block;
    module_segment = segment;
    return true;
}

uint32_t i18n_segment_loaded(void)
{
    return module_segment;
}

void i18n_detach(void)
{
    attached = NULL;
    i18n_detach_segment();
}

bool i18n_attached(void)
{
    return attached != NULL;
}

const char *i18n_code(void)
{
    return attached != NULL ? (const char *)attached + CODE_FIELD : "";
}

static const char *fallback_text(StringId id)
{
#ifdef ECHO_BUILTIN_ENGLISH
    return STRINGS_ENGLISH[id];
#else
    (void)id;
    return "?";
#endif
}

const char *i18n_text(StringId id)
{
    if ((uint32_t)id >= STRING_COUNT) {
        return "";
    }
    uint32_t segment = 0;
    while ((uint32_t)id >= FIRSTS[segment + 1]) {
        segment++;
    }
    const uint8_t *block = segment == module_segment ? module_block : NULL;
    if (segment == 0 && attached != NULL) {
        block = attached + I18N_INDEX_BYTES;
    }
    if (block == NULL) {
        return fallback_text(id);
    }
    uint32_t index = (uint32_t)id - FIRSTS[segment];
    uint32_t offset = bytes_le16(block + index * OFFSET_BYTES);
    return (const char *)block + OFFSET_BYTES * strings_in(segment) + offset;
}

static bool is_placeholder(const char *cursor)
{
    return cursor[0] == '{' && cursor[1] >= '0' && cursor[1] <= '9' && cursor[2] == '}';
}

void i18n_format(StringBuilder *builder, StringId pattern, const char *const *arguments,
                 uint32_t count)
{
    for (const char *cursor = i18n_text(pattern); *cursor != '\0'; cursor++) {
        if (!is_placeholder(cursor)) {
            string_builder_append_char(builder, *cursor);
            continue;
        }
        uint32_t index = (uint32_t)(cursor[1] - '0');
        if (index < count) {
            string_builder_append(builder, arguments[index]);
        }
        cursor += 2;
    }
}

void i18n_format_text(StringBuilder *builder, StringId pattern, const char *argument)
{
    i18n_format(builder, pattern, &argument, 1);
}

void i18n_format_counts(StringBuilder *builder, StringId pattern, uint32_t first, uint32_t second)
{
    char text[2][NUMBER_CAPACITY];
    StringBuilder first_text = string_builder_make(text[0], NUMBER_CAPACITY);
    StringBuilder second_text = string_builder_make(text[1], NUMBER_CAPACITY);
    string_builder_append_unsigned(&first_text, first);
    string_builder_append_unsigned(&second_text, second);
    const char *arguments[2] = {text[0], text[1]};
    i18n_format(builder, pattern, arguments, 2);
}
