#include "formal.h"
#include "tags_id3.c"

void harness(void)
{
    (void)skip_value(NULL, nondet_uint32(), nondet_uint32(), nondet_uint32());
}
