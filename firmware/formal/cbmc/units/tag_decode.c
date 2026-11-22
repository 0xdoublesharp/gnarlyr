#include "formal.h"
#include "tags_internal.h"

void harness(void)
{
    tag_decode(NULL, NULL, nondet_uint32(), (TagEncoding)nondet_uint8());
}
