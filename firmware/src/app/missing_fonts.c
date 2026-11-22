#include "missing_fonts.h"

#include <stddef.h>

#include "core/bytes.h"
#include "core/drive_path.h"
#include "core/utf8.h"
#include "hal/storage.h"

enum {
    READ_BYTES = 256
};

const char MISSING_FONTS_PATH[] =
    DRIVE_PATH_INTERNAL_ROOT "/" FIRMWARE_DATA_DIRECTORY "/fonts/missing.txt";
const char MISSING_FONTS_COUNT_PATH[] =
    DRIVE_PATH_INTERNAL_ROOT "/" FIRMWARE_DATA_DIRECTORY "/fonts/missing.count";

bool missing_fonts_undrawable(const Font *font, uint32_t codepoint)
{
    if (codepoint < 0x20 || (codepoint >= 0x7F && codepoint < 0xA0) || codepoint > 0xFFFF ||
        codepoint == UTF8_REPLACEMENT_CHARACTER) {
        return false;
    }
    const Glyph *glyph = font_find_glyph(font, codepoint);
    return glyph == NULL || glyph->codepoint != codepoint;
}

static bool still_missing(HalFile file, const Font *font)
{
    char chunk[READ_BYTES + 1];
    uint32_t carry = 0;
    for (uint32_t offset = 0; offset < file.size;) {
        uint32_t take =
            file.size - offset < READ_BYTES - carry ? file.size - offset : READ_BYTES - carry;
        if (!hal_file_read(file, offset, chunk + carry, take)) {
            return false;
        }
        offset += take;
        uint32_t length = carry + take;
        uint32_t whole = (uint32_t)utf8_complete_length(chunk, length);
        chunk[whole] = '\0';
        const char *cursor = chunk;
        for (uint32_t codepoint = utf8_next(&cursor); codepoint != 0;
             codepoint = utf8_next(&cursor)) {
            if (missing_fonts_undrawable(font, codepoint)) {
                return true;
            }
        }
        if ((uint32_t)(cursor - chunk) < whole) {
            return false;
        }
        carry = length - whole;
        for (uint32_t index = 0; index < carry; index++) {
            chunk[index] = chunk[whole + index];
        }
    }
    return false;
}

uint32_t missing_fonts_names(const Font *font)
{
    uint8_t count[MISSING_FONTS_COUNT_BYTES];
    HalFile file;
    if (!hal_file_open(&file, MISSING_FONTS_COUNT_PATH)) {
        return 0;
    }
    bool read = file.size >= sizeof count && hal_file_read(file, 0, count, sizeof count);
    hal_file_close(&file);
    uint32_t names = read ? bytes_le32(count) : 0;
    if (names == 0 || !hal_file_open(&file, MISSING_FONTS_PATH)) {
        return 0;
    }
    bool missing = still_missing(file, font);
    hal_file_close(&file);
    return missing ? names : 0;
}
