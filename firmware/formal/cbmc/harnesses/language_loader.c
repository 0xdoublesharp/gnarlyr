#include "core/i18n.h"
#include "formal.h"
#include "language.h"

void harness(void)
{
    char code[I18N_CODE_BYTES];
    formal_card_fill();
    __CPROVER_havoc_object(code);
    code[I18N_CODE_BYTES - 1] = '\0';
    (void)language_load(code);
    (void)language_use_module(nondet_uint32());
}
