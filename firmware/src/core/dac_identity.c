#include "core/dac_identity.h"

#include <stdbool.h>

#include "core/string_builder.h"

enum {
    ID_HIGH_NIBBLE = 0xF0,
    NIBBLE_BITS = 4,
    NIBBLE_MASK = 0x0F
};

typedef struct KnownPart {
    uint8_t id[DAC_IDENTITY_ID_BYTES];
    uint8_t part;
} KnownPart;

static const KnownPart KNOWN_PARTS[] = {
    {{0x43, 0x13, 0x10}, DAC_PART_CS43131},
    {{0x43, 0x19, 0x80}, DAC_PART_CS43198},
};

static bool was_read(const DacIdentity *identity, int index)
{
    return ((identity->read_mask >> index) & 1u) != 0;
}

static bool is_complete(const DacIdentity *identity)
{
    return identity->source == DAC_IDENTITY_READ && identity->read_mask == DAC_IDENTITY_ALL_READ;
}

static bool id_matches(const DacIdentity *identity, const uint8_t *id)
{
    return identity->bytes[0] == id[0] && identity->bytes[1] == id[1] &&
           (identity->bytes[2] & ID_HIGH_NIBBLE) == id[2];
}

DacPart dac_identity_part(const DacIdentity *identity)
{
    if (!is_complete(identity)) {
        return DAC_PART_UNKNOWN;
    }
    for (size_t index = 0; index < sizeof KNOWN_PARTS / sizeof KNOWN_PARTS[0]; index++) {
        if (id_matches(identity, KNOWN_PARTS[index].id)) {
            return (DacPart)KNOWN_PARTS[index].part;
        }
    }
    return DAC_PART_UNKNOWN;
}

static const char *words_for(const DacIdentity *identity)
{
    if (identity->source == DAC_IDENTITY_SIMULATED) {
        return "Simulated";
    }
    if (identity->source != DAC_IDENTITY_READ) {
        return "Play a track";
    }
    return identity->read_mask == 0 ? "Read failed" : NULL;
}

static void append_byte(StringBuilder *builder, const DacIdentity *identity, int index)
{
    static const char DIGITS[] = "0123456789ABCDEF";
    if (!was_read(identity, index)) {
        string_builder_append(builder, "--");
        return;
    }
    string_builder_append_char(builder, DIGITS[identity->bytes[index] >> NIBBLE_BITS]);
    string_builder_append_char(builder, DIGITS[identity->bytes[index] & NIBBLE_MASK]);
}

void dac_identity_text(const DacIdentity *identity, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    const char *words = words_for(identity);
    if (words != NULL) {
        string_builder_append(&builder, words);
        return;
    }
    if (is_complete(identity) && dac_identity_part(identity) == DAC_PART_UNKNOWN) {
        string_builder_append_char(&builder, '?');
    }
    for (int index = 0; index < DAC_IDENTITY_ID_BYTES; index++) {
        append_byte(&builder, identity, index);
    }
    string_builder_append_char(&builder, ' ');
    append_byte(&builder, identity, DAC_IDENTITY_REVISION);
    string_builder_append_char(&builder, '.');
    append_byte(&builder, identity, DAC_IDENTITY_SUBREVISION);
}
