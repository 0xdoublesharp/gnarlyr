#include "formal.h"
#include "tags_id3.c"

void harness(void)
{
    read_frames(NULL, NULL, nondet_uint32());
}
