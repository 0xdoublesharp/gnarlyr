#include "core/i18n.h"

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
    OFFSET_BYTES = 2,
    TEXT_BASE = I18N_HEADER_BYTES + STRING_COUNT * OFFSET_BYTES,
    BYTE_BITS = 8,
    NUMBER_CAPACITY = 12
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'D', 'S', 'L', 'P'};

static const uint8_t *attached;

static uint32_t read_u16(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << BYTE_BITS;
}

static bool header_matches(const uint8_t *header)
{
    for (int index = 0; index < MAGIC_SIZE; index++) {
        if (header[index] != MAGIC[index]) {
            return false;
        }
    }
    return read_u16(header + VERSION_FIELD) == I18N_VERSION &&
           read_u16(header + COUNT_FIELD) == STRING_COUNT &&
           hash_load(header + CATALOGUE_FIELD) == STRINGS_HASH &&
           header[CODE_FIELD + I18N_CODE_BYTES - 1] == '\0' &&
           header[NAME_FIELD + I18N_NAME_BYTES - 1] == '\0';
}

bool i18n_pack_info(const uint8_t *header, size_t length, I18nPackInfo *info)
{
    if (header == NULL || length < I18N_HEADER_BYTES || !header_matches(header)) {
        return false;
    }
    info->size = hash_load(header + SIZE_FIELD);
    for (int index = 0; index < I18N_CODE_BYTES; index++) {
        info->code[index] = (char)header[CODE_FIELD + index];
    }
    for (int index = 0; index < I18N_NAME_BYTES; index++) {
        info->name[index] = (char)header[NAME_FIELD + index];
    }
    return info->size > TEXT_BASE;
}

static bool body_is_sound(const uint8_t *pack, uint32_t size)
{
    uint32_t checksum = hash_update(HASH_START, pack, CHECKSUM_FIELD);
    checksum = hash_update(checksum, pack + CHECKSUM_END, size - CHECKSUM_END);
    if (checksum != hash_load(pack + CHECKSUM_FIELD) || pack[size - 1] != '\0') {
        return false;
    }
    for (uint32_t id = 0; id < STRING_COUNT; id++) {
        if (TEXT_BASE + read_u16(pack + I18N_HEADER_BYTES + id * OFFSET_BYTES) >= size) {
            return false;
        }
    }
    return true;
}

bool i18n_attach(const uint8_t *pack, size_t length)
{
    I18nPackInfo info;
    i18n_detach();
    if (!i18n_pack_info(pack, length, &info) || info.size > length ||
        !body_is_sound(pack, info.size)) {
        return false;
    }
    attached = pack;
    return true;
}

void i18n_detach(void)
{
    attached = NULL;
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
    if (attached == NULL) {
        return fallback_text(id);
    }
    uint32_t offset = read_u16(attached + I18N_HEADER_BYTES + (uint32_t)id * OFFSET_BYTES);
    return (const char *)attached + TEXT_BASE + offset;
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
