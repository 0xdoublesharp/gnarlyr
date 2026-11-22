#include "formal.h"
#include "tags_internal.h"

void harness(void)
{
    (void)tag_read(NULL, nondet_uint32(), NULL, nondet_uint32());
}
