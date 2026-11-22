#include "formal.h"
#include "theme_pack.c"

void harness(void)
{
    (void)text_valid(NULL, nondet_uint32(), nondet_bool());
}
