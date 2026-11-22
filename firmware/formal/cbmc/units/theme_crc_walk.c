#include "formal.h"
#include "theme_pack.c"

void harness(void)
{
    (void)crc_valid(formal_card_source(), nondet_uint32(), nondet_uint32());
}
