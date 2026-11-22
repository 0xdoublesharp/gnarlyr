#include "formal.h"
#include "tags_internal.h"

void harness(void)
{
    tag_set_duration(NULL, ((uint64_t)nondet_uint32() << 32) | nondet_uint32(),
                     ((uint64_t)nondet_uint32() << 32) | nondet_uint32());
}
