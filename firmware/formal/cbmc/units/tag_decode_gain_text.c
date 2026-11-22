#include "formal.h"
#include "tags_internal.h"

void harness(void)
{
    (void)tag_decode_gain_text(NULL, NULL, nondet_uint32(), (TagEncoding)nondet_uint8());
}
