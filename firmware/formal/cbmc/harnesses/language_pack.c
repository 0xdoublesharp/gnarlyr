#include "core/i18n.h"
#include "formal.h"

static uint8_t pack[FORMAL_CARD_BYTES];
static uint8_t block[FORMAL_LANGUAGE_MEMORY];
static const uint16_t FIRSTS[STRING_SEGMENT_COUNT + 1] = STRING_SEGMENT_FIRSTS;

static void check_text(StringId id, const uint8_t *base, uint32_t size)
{
    uint32_t at = (uint32_t)((const uint8_t *)i18n_text(id) - base);
    __CPROVER_assert(at < size, "a looked-up text starts inside its block");
    bool ended = false;
    for (uint32_t index = at; index < size && !ended; index++) {
        ended = base[index] == '\0';
    }
    __CPROVER_assert(ended, "a looked-up text ends inside its block");
}

void harness(void)
{
    I18nSegment row;
    __CPROVER_havoc_object(pack);
    __CPROVER_havoc_object(block);
    uint32_t length = formal_below(FORMAL_CARD_BYTES + 1);
    if (!i18n_attach(pack, length)) {
        return;
    }
    check_text((StringId)formal_below(FIRSTS[1]), pack, length);
    uint32_t segment = 1 + formal_below(STRING_SEGMENT_COUNT - 1);
    if (i18n_pack_segment(pack, segment, &row) && row.size <= FORMAL_LANGUAGE_MEMORY &&
        i18n_attach_segment(segment, block, &row)) {
        uint32_t first = FIRSTS[segment];
        check_text((StringId)(first + formal_below(FIRSTS[segment + 1] - first)), block, row.size);
    }
}
