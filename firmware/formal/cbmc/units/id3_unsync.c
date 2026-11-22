#include "formal.h"
#include "tags_id3.c"

void harness(void)
{
    (void)remove_unsync(NULL, nondet_uint32());
}
