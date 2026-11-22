#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx/font.h"

enum {
    MISSING_FONTS_FILE_BYTES = 16384,
    MISSING_FONTS_COUNT_BYTES = 4,
    MISSING_FONTS_NOTICE_BYTES = 128
};

extern const char MISSING_FONTS_PATH[];
extern const char MISSING_FONTS_COUNT_PATH[];

bool missing_fonts_undrawable(const Font *font, uint32_t codepoint);
void missing_fonts_record(void *memory, uint32_t size, const Font *font);
uint32_t missing_fonts_names(const Font *font);
const char *missing_fonts_notice(char *text, uint32_t capacity, const Font *font);
