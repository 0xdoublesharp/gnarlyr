#include "cue_sheet.c"
#include "formal.h"

void harness(void)
{
    cue_sheet_open(NULL, formal_card_source(), nondet_uint32());
}
