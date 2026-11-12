#include "fat_names.h"

static size_t utf8_length_of(uint32_t code_point)
{
    if (code_point < 0x80) {
        return 1;
    }
    if (code_point < 0x800) {
        return 2;
    }
    return code_point < 0x10000 ? 3 : 4;
}

static void put_utf8(uint32_t code_point, size_t length, char *out)
{
    static const uint8_t LEADS[5] = {0, 0x00, 0xC0, 0xE0, 0xF0};
    for (size_t index = length - 1; index > 0; index--) {
        out[index] = (char)(0x80 | (code_point & 0x3F));
        code_point >>= 6;
    }
    out[0] = (char)(LEADS[length] | code_point);
}

size_t fat_utf8_from_units(const uint16_t *units, size_t limit, char *out, size_t capacity)
{
    size_t unit = 0;
    size_t written = 0;
    uint32_t code_point = 0;
    while (fat_next_utf16(units, limit, &unit, &code_point)) {
        size_t length = utf8_length_of(code_point);
        if (written + length + 1 > capacity) {
            break;
        }
        put_utf8(code_point, length, out + written);
        written += length;
    }
    out[written] = '\0';
    return written;
}
