#include "formal.h"
#include "tags_id3.c"

void harness(void)
{
    (void)tags_skip_id3v2(NULL, nondet_bool());
}
