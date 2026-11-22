#include "formal.h"
#include "tags_id3.c"

void harness(void)
{
    (void)tags_read_id3v2(NULL, nondet_uint32(), NULL);
}
