#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/string_builder.h"
#include "core/strings.h"

enum {
    I18N_VERSION = 1,
    I18N_HEADER_BYTES = 56,
    I18N_SEGMENT_BYTES = 16,
    I18N_INDEX_BYTES = I18N_HEADER_BYTES + 4 + STRING_SEGMENT_COUNT * I18N_SEGMENT_BYTES,
    I18N_CODE_BYTES = 8,
    I18N_NAME_BYTES = 28,
    I18N_PACK_CAPACITY = 18432,
    I18N_FILE_CAPACITY = 65535,
    I18N_MAX_ARGUMENTS = 10
};

typedef struct I18nPackInfo {
    uint32_t size;
    uint32_t resident;
    char code[I18N_CODE_BYTES];
    char name[I18N_NAME_BYTES];
} I18nPackInfo;

typedef struct I18nSegment {
    uint32_t offset;
    uint32_t size;
    uint32_t checksum;
} I18nSegment;

bool i18n_pack_info(const uint8_t *index, size_t length, I18nPackInfo *info);
bool i18n_pack_segment(const uint8_t *index, uint32_t segment, I18nSegment *row);
bool i18n_attach(const uint8_t *pack, size_t length);
bool i18n_attach_segment(uint32_t segment, const uint8_t *block, const I18nSegment *row);
void i18n_detach_segment(void);
uint32_t i18n_segment_loaded(void);
void i18n_detach(void);
bool i18n_attached(void);
const char *i18n_code(void);
const char *i18n_text(StringId id);
void i18n_format(StringBuilder *builder, StringId pattern, const char *const *arguments,
                 uint32_t count);
void i18n_format_text(StringBuilder *builder, StringId pattern, const char *argument);
void i18n_format_counts(StringBuilder *builder, StringId pattern, uint32_t first, uint32_t second);
