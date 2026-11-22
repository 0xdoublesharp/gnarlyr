#include "formal.h"
#include "tags_internal.h"

void harness(void)
{
    (void)tags_mp4_read_atom(NULL, nondet_uint32(), nondet_uint32(), NULL);
}
