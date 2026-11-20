#include "core/i18n.h"
#include "formal.h"
#include "language.h"

static void check_text(StringId id)
{
    const char *text = i18n_text(id);
    bool ended = false;
    for (uint32_t index = 0; index < FORMAL_LANGUAGE_MEMORY && !ended; index++) {
        ended = text[index] == '\0';
    }
    __CPROVER_assert(ended, "a looked-up text ends inside the attached pack");
}

void harness(void)
{
    char code[I18N_CODE_BYTES];
    formal_card_fill();
    __CPROVER_havoc_object(code);
    code[I18N_CODE_BYTES - 1] = '\0';
    (void)language_load(code);
    if (i18n_attached()) {
        check_text((StringId)formal_below(STRING_COUNT));
        __CPROVER_assert(i18n_code()[I18N_CODE_BYTES - 1] == '\0',
                         "the pack code ends in its field");
    }
    (void)language_use_module(formal_below(STRING_SEGMENT_COUNT + 1));
    if (i18n_attached()) {
        check_text((StringId)formal_below(STRING_COUNT));
    }
}
