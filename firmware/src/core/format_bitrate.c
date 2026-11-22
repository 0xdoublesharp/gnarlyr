#include "core/format.h"

#include "core/string_builder.h"

enum {
    BITRATE_SMALL = 10,
    BITRATE_GROUP = 1000
};

static void append_padded(StringBuilder *builder, uint32_t value, uint32_t below)
{
    for (; below > 1 && value < below; below /= BITRATE_SMALL) {
        string_builder_append_char(builder, '0');
    }
    string_builder_append_unsigned(builder, value);
}

void format_bitrate(uint32_t kbps, char *buffer, size_t capacity)
{
    StringBuilder builder = string_builder_make(buffer, capacity);
    uint32_t scale = 1;
    while (kbps / scale >= BITRATE_GROUP) {
        scale *= BITRATE_GROUP;
    }
    string_builder_append_unsigned(&builder, kbps / scale);
    while (scale > 1) {
        kbps %= scale;
        scale /= BITRATE_GROUP;
        string_builder_append_char(&builder, ',');
        append_padded(&builder, kbps / scale, BITRATE_SMALL * BITRATE_SMALL);
    }
    string_builder_append(&builder, " kbps");
}
